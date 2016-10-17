#include <stdexcept>
#include <string>
#include <iostream>
#include <system_error>

#include <cstring>

#include <unistd.h>
#include <netinet/in.h>
#include <linux/net_tstamp.h>

#include "util.h"
#include "sender.h"

using std::stoi;
using std::cout;
using std::string;

void check_equal_addresses(sockaddr_storage *ss1, sockaddr_storage *ss2)
{

    if (ss1->ss_family != ss2->ss_family)
    {
        throw std::runtime_error("Sender addr does not match expected addr!");
    }
    else if (ss1->ss_family == AF_INET)
    {
        if (((sockaddr_in *)ss1)->sin_addr.s_addr != ((sockaddr_in *)ss2)->sin_addr.s_addr)
        {
            throw std::runtime_error("Sender addr does not match expected addr!");
        }
    }
    else if (ss1->ss_family == AF_INET6)
    {
        if ( memcmp(&((sockaddr_in6 *)ss1)->sin6_addr, &((sockaddr_in6 *)ss2)->sin6_addr, sizeof(in6_addr)) )
        {
            throw std::runtime_error("Sender addr does not match expected addr!");
        }
    }
    else
    {
        throw std::runtime_error("Address neither IPv4 or v6!");
    }
}

void receive_loop(string address, in_port_t listen_port, int domain, string iface_name)
{
    int sock;
    const time_t SLEEP_TIME = 5;
    char *databuf = 0;
    int datalen = 0;
    sockaddr_storage *ss;
    sockaddr_storage expected_sender_addr;
    create_sockaddr_storage(domain, address, listen_port, &expected_sender_addr);
    sock = setup_socket(domain, SOCK_DGRAM, SOF_TIMESTAMPING_TX_HARDWARE | SOF_TIMESTAMPING_RAW_HARDWARE);
    set_nonblocking(sock);
    setup_device(sock, iface_name, SOF_TIMESTAMPING_TX_HARDWARE);

    do_bind(sock, &expected_sender_addr);

    for (;;)
    {
        fd_set efds;
        fd_set rfds;
        timespec ts;
        int retval;

        FD_ZERO(&efds);
        FD_ZERO(&rfds);
        FD_SET(sock, &efds);
        FD_SET(sock, &rfds);

        /* Wait up to five seconds. */
        ts.tv_sec = SLEEP_TIME;
        ts.tv_nsec = 0;

        retval = pselect(sock+1, &rfds, NULL, &efds, &ts, NULL);
        if (retval == 0)
        {
            cout << "Sleept " << SLEEP_TIME << "seconds without traffic...\n";
            continue;
        }

        if (FD_ISSET(sock, &rfds))
        {
            recvpacket(sock, 0, &databuf, &datalen, &ss);
            if (datalen == 0)
            {
                cout << "sock marked as readable by select, but no data read!\n";
                continue;
            }

            // TODO: Prepare reflected pkt
#ifdef DEBUG
            // Compare address from recvpacket with cmd-line supplied expected sender addr.
            check_equal_addresses(ss, &expected_sender_addr);
#endif
            // bounce the packet back
            sendpacket(ss, sock, databuf, datalen);
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
        if (argc != 6)
        {
            throw std::runtime_error("Usage: receiver <sender ip addr> <listen port> <ip ver (4 or 6)> <iface>");
        }
        else
        {
            address = string(argv[1]);
            port = stoi(argv[2]);
            ipver = stoi(argv[3]);
            domain = ipver == 6 ? AF_INET6 : AF_INET;
            iface_name = string(argv[4]);
        }

        receive_loop(address, port, domain, iface_name);
    }
    catch (std::exception &exc)
    {
        cout << "Got exception: " << exc.what() << '\n';
        exit(1);
    }

    return 0;
}
