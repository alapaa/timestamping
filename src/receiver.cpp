#include <stdexcept>
#include <string>
#include <iostream>
#include <system_error>

#include <cstring>
#include <cassert>

#include <unistd.h>
#include <netinet/in.h>
#include <linux/net_tstamp.h>
#include <bsd/string.h> // strlcpy: apt-get install libbsd-dev on *buntu and Debian.

#include "logging.h"
#include "packet.h"
#include "util.h"
#include "sender.h"

using std::stoi;
using std::string;
using std::tuple;
using std::shared_ptr;

using namespace Netrounds;

void receive_loop(string address, in_port_t listen_port, int domain, string iface_name)
{
    int sock;
    const time_t SLEEP_TIME = 20;
    shared_ptr<char> data;
    int datalen = 0;
    sockaddr_storage ss;
    sockaddr_storage bind_addr;
    uint32_t prev_sender_seq = 0;

    fd_set efds;
    fd_set rfds;
    timespec ts;
    int retval;

    shared_ptr<SenderPacket> pkt;
    shared_ptr<ReflectorPacket> retpkt;

    timespec t2;
    timespec t2_prev;
    timespec t3_prev;

    memset(&t2, 0, sizeof(t2));
    memset(&t2_prev, 0, sizeof(t2_prev));
    memset(&t3_prev, 0, sizeof(t3_prev));

    sock = setup_socket(domain, SOCK_DGRAM, SOF_TIMESTAMPING_TX_HARDWARE | SOF_TIMESTAMPING_RX_HARDWARE | SOF_TIMESTAMPING_RAW_HARDWARE);
    set_nonblocking(sock);
    setup_device(sock, iface_name, SOF_TIMESTAMPING_TX_HARDWARE | SOF_TIMESTAMPING_RX_HARDWARE);

    create_sockaddr_storage(domain, address, listen_port, &bind_addr);
    do_bind(sock, &bind_addr);

    uint32_t refl_counter = 1234;
    for (;;)
    {
        FD_ZERO(&efds);
        FD_ZERO(&rfds);
        FD_SET(sock, &efds);
        FD_SET(sock, &rfds);

        /* Wait up to five seconds. */
        ts.tv_sec = SLEEP_TIME;
        ts.tv_nsec = 0;

        retval = pselect(sock+1, &rfds, NULL, &efds, &ts, NULL);
        if (retval == -1)
        {
            throw std::system_error(errno, std::system_category());
        }
        if (retval == 0)
        {
            logdebug << "Slept " << SLEEP_TIME << " seconds without traffic...\n";
            continue;
        }

        if (FD_ISSET(sock, &rfds))
        {
            t2_prev = t2;
            logdebug << "Assigned t2_prev ";
            logdebug << t2_prev << '\n';
            tie(data, datalen, ss, t2) = recvpacket(sock, 0);
            logdebug << "Got new T2 " << t2 << '\n';
            if (datalen == 0)
            {
                logdebug << "sock marked as readable by select(), but no data read!\n";
                continue;
            }
            pkt = deserialize_packet(data.get(), datalen);

            // bounce the packet back
            retpkt.reset(new ReflectorPacket);
            memset(retpkt.get(), 0, sizeof(*retpkt));
            retpkt->type = FROM_REFLECTOR;
            retpkt->sender_seq = pkt->sender_seq;
            retpkt->refl_seq = refl_counter++;
            if (prev_sender_seq == (pkt->sender_seq - 1))
            {
                logdebug << "OK, piggybacking prev pkt T2 and T3, prev pkt seqnr " << prev_sender_seq << '\n';
                logdebug << t2_prev << '\n';
                logdebug << t3_prev << '\n';
                retpkt->t2_sec = t2_prev.tv_sec;
                retpkt->t2_nsec = t2_prev.tv_nsec;
                retpkt->t3_sec = t3_prev.tv_sec;
                retpkt->t3_nsec = t3_prev.tv_nsec;
            }
            else
            {
                logdebug << "Missed prev pkt, cannot piggyback hw timestaps\n";
            }
            prev_sender_seq = pkt->sender_seq;
            tie(data, datalen) = serialize_reflector_packet(retpkt);
            sendpacket(&ss, sock, data.get(), datalen);
            logdebug << "Sent reply, now get HW send timestamp...\n";
            wait_for_errqueue_data(sock);
            tie(data, datalen, ss, t3_prev) = receive_send_timestamp(sock);
            logdebug << "Got new T3 prev ";
            logdebug << t3_prev << '\n';
            if (!check_seqnr(data, datalen, pkt->sender_seq))
            {
                throw std::runtime_error("Seq nr check failed!");
            }
        }
    }
}


int main(int argc, char *argv[])
{
    string address;
    in_port_t port;
    int domain;
    int ipver;
    string iface_name;

    try
    {
        if (argc != 5)
        {
            throw std::runtime_error("Usage: receiver <bind ip (can be 0.0.0.0)> <bind port> <ip ver (4 or 6)> <iface>");
        }
        else
        {
            address = string(argv[1]);
            port = stoi(argv[2]);
            ipver = stoi(argv[3]);
            domain = ipver == 6 ? AF_INET6 : AF_INET;
            iface_name = string(argv[4]);
        }
        INIT_LOGGING("/tmp/tslog.txt", LOG_DEBUG);
        receive_loop(address, port, domain, iface_name);
    }
    catch (std::exception &exc)
    {
        logdebug << "Got exception: " << exc.what() << '\n';
        exit(1);
    }

    return 0;
}
