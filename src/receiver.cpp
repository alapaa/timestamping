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
using std::tuple;
using std::shared_ptr;

void receive_loop(string address, in_port_t listen_port, int domain, string iface_name)
{
    int sock;
    const time_t SLEEP_TIME = 5;
    shared_ptr<char> data;
    int datalen = 0;
    sockaddr_storage ss;
    sockaddr_storage bind_addr;

    sock = setup_socket(domain, SOCK_DGRAM, SOF_TIMESTAMPING_TX_HARDWARE | SOF_TIMESTAMPING_RAW_HARDWARE);
    set_nonblocking(sock);
    setup_device(sock, iface_name, SOF_TIMESTAMPING_TX_HARDWARE);

    create_sockaddr_storage(domain, address, listen_port, &bind_addr);
    do_bind(sock, &bind_addr);

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
        if (retval == -1)
        {
            throw std::system_error(errno, std::system_category());
        }
        if (retval == 0)
        {
            cout << "Slept " << SLEEP_TIME << " seconds without traffic...\n";
            continue;
        }

        if (FD_ISSET(sock, &rfds))
        {
            tie(data, datalen, ss) = recvpacket(sock, 0);
            if (datalen == 0)
            {
                cout << "sock marked as readable by select, but no data read!\n";
                continue;
            }

            // TODO: Prepare reflected pkt
#ifdef DEBUG
            // Compare address from recvpacket with expected sender addr.
            //check_equal_addresses(&ss, &bind_addr);
#endif
            // bounce the packet back
            sendpacket(&ss, sock, data.get(), datalen);
            cout << "Sent reply, now get HW send timestamp...\n";
            wait_for_errqueue_data(sock);
            receive_send_timestamp(sock);
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

        receive_loop(address, port, domain, iface_name);
    }
    catch (std::exception &exc)
    {
        cout << "Got exception: " << exc.what() << '\n';
        exit(1);
    }

    return 0;
}
