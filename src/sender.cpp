#include <stdexcept>
#include <string>
#include <iostream>
#include <system_error>
#include <memory>

#include <unistd.h>
#include <netinet/in.h>
#include <linux/net_tstamp.h>

#include "util.h"
#include "packet.h"
#include "sender.h"

using std::stoi;
using std::cout;
using std::string;
using std::shared_ptr;

//using Netrounds::make_timespec;
//using Netrounds::prepare_packet;
//using Netrounds::deserialize_reflector_packet;
//using Netrounds::ReflectorPacket;
using namespace Netrounds;

int main(int argc, char *argv[])
{
    int nr_packets = 0;
    int sock;
    string address;
    in_port_t port;
    int domain;
    int ipver = 0;
    string iface_name;

    const size_t BUFLEN = 1472;
    char buf[BUFLEN];

    shared_ptr<char> data;
    size_t datalen;
    sockaddr_storage ss;

    const timespec ZERO_TS = {0, 0};
    timespec t1 = ZERO_TS;
    timespec t4 = ZERO_TS;
    timespec t1_prev;
    timespec t2_prev;
    timespec t3_prev;
    timespec t4_prev;

    timespec rtt_soft;
    timespec rtt_hard;
    timespec delay_on_refl;

    timespec initial_clock_diff {0, 0};
    int result;

    try
    {
        if (argc != 6)
        {
            throw std::runtime_error("Usage: sender <ip addr> <port> <ip ver (4 or 6)> <nr of packets> <iface>");
        }
        else
        {
            address = string(argv[1]);
            port = stoi(argv[2]);
            ipver = stoi(argv[3]);
            domain = ipver == 6 ? AF_INET6 : AF_INET;
            nr_packets = stoi(argv[4]);
            iface_name = string(argv[5]);
        }

        sock = setup_socket(domain, SOCK_DGRAM, SOF_TIMESTAMPING_TX_HARDWARE | SOF_TIMESTAMPING_RX_HARDWARE | SOF_TIMESTAMPING_RAW_HARDWARE);
        setup_device(sock, iface_name, SOF_TIMESTAMPING_TX_HARDWARE | SOF_TIMESTAMPING_RX_HARDWARE);

        uint32_t send_counter = 0;
        for (; nr_packets; nr_packets--)
        {
            prepare_packet(buf, BUFLEN, send_counter++);
            sendpacket(domain, address, port, sock, buf, BUFLEN);
            wait_for_errqueue_data(sock);
            t1_prev = t1;
            t4_prev = t4;
            tie(data, datalen, ss, t1) = receive_send_timestamp(sock);
            tie(data, datalen, ss, t4) = recvpacket(sock, 0);
            if (!data)
            {
                cout << "----------No data from sender at lap " << (send_counter - 1) << '\n';
                continue;
            }
            shared_ptr<ReflectorPacket> rp = deserialize_reflector_packet(data, datalen);
            if (rp->sender_seq != (send_counter-1))
            {
                cout << "---------- REFLECTED SEQNR " << rp->sender_seq << "DOES NOT MATCH SEND SEQNR " <<
                    (send_counter-1) << '\n';
            }
            t2_prev = make_timespec(rp->t2_sec, rp->t2_nsec);
            t3_prev = make_timespec(rp->t3_sec, rp->t3_nsec);

            print_ts(t1_prev);
            print_ts(t2_prev);
            print_ts(t3_prev);
            print_ts(t4_prev);

            if (t1_prev != ZERO_TS && t4_prev != ZERO_TS &&
                t2_prev != ZERO_TS && t3_prev != ZERO_TS)
            {
                if (initial_clock_diff == ZERO_TS)
                {
                    initial_clock_diff = subtract_ts(t1_prev, t2_prev);
                    cout << "Setting initial clock diff: ";
                    print_ts(initial_clock_diff);
                    cout << '\n';
                }
                else
                {
                    cout << "Showing initial clock diff: ";
                    print_ts(initial_clock_diff);
                    cout << '\n';

                    cout << "Clock diff sender<->reflector: ";
                    timespec clockdiff = subtract_ts(t1_prev, t2_prev);
                    print_ts(clockdiff);
                    cout << "Clock drift since start: ";
                    print_ts( subtract_ts(clockdiff, initial_clock_diff) );
                }

                rtt_soft = subtract_ts(t4_prev, t1_prev);
                delay_on_refl = subtract_ts(t3_prev, t2_prev);
                rtt_hard = subtract_ts(rtt_soft, delay_on_refl);

                cout << "rtt soft: ";
                print_ts(rtt_soft);
                cout << "rtt hard: ";
                print_ts(rtt_hard);
            }
            else
            {
                cout << "---- missing values in 4-tuple T1 - T4, cannot compute\n";
            }

            cout << "Sleeping...\n";
            timespec currtime;
            clock_gettime(CLOCK_MONOTONIC, &currtime);
            const long long int INTERVAL_NANOSEC = 1000000000;
            int remain_sleeplen_ns = -1* (currtime.tv_nsec % INTERVAL_NANOSEC) + INTERVAL_NANOSEC;
            cout << "sleeplen " << remain_sleeplen_ns << '\n';
            result = usleep(remain_sleeplen_ns/1000);
            if (result == -1)
            {
                throw std::system_error(errno, std::system_category());
            }
        }
    }
    catch (std::exception &exc)
    {
        cout << "Got exception: " << exc.what() << '\n';
        exit(1);
    }

    return 0;
}
