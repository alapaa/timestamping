#include <stdexcept>
#include <string>
#include <iostream>
#include <system_error>

#include <unistd.h>
#include <netinet/in.h>
#include <linux/net_tstamp.h>

#include "util.h"
#include "packet.h"
#include "sender.h"

using std::stoi;
using std::cout;
using std::string;

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
            prepare_packet(buf, BUFLEN, send_counter);
            sendpacket(domain, address, port, sock, buf, BUFLEN);
            send_counter++;
            wait_for_errqueue_data(sock);
            receive_send_timestamp(sock);
            cout << "Sleeping...\n";
            sleep(5);
        }
    }
    catch (std::exception &exc)
    {
        cout << "Got exception: " << exc.what() << '\n';
        exit(1);
    }

    return 0;
}
