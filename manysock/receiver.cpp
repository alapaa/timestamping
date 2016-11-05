#include <iostream>
#include <thread>
#include <vector>
#include <cassert>
#include <system_error>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <unistd.h>

#include <sys/epoll.h>

#include "util.h"
#include "logging.h"

using std::thread;
using std::vector;
using std::cout;
using std::system_error;
using std::atomic;
using std::string;
using std::stoi;
using std::to_string;

using namespace Netrounds;

atomic<uint64_t> *byte_count;
atomic<uint64_t> *pkt_count;

int receiver_thread(string receiver_ip, in_port_t start_port, int nr_streams, const int worker_nr)
{
    const size_t PKT_PAYLOAD = 32;
    char buf[PKT_PAYLOAD];
    int result;
    int tmp;
    int recv_bytes;

    struct epoll_event ev;
    struct epoll_event events[nr_streams];
    int epollfd;
    int nfds;
    int bufsz = 1000000;
    socklen_t optlen;

    vector<int> sockets(nr_streams);
    sockaddr_storage receiver_addr;
    create_sockaddr_storage(AF_INET, receiver_ip, start_port, &receiver_addr);

    sockaddr_in raddr = *reinterpret_cast<sockaddr_in *>(&receiver_addr);

    for (int i = 0; i < nr_streams; i++)
    {
        sockets[i] = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (sockets[i] == -1)
        {
            throw std::system_error(errno, std::system_category(), FILELINE);
        }
        result = setsockopt(sockets[i], SOL_SOCKET, SO_RCVBUF, (const char *)&bufsz, sizeof(bufsz));
        if (result == -1)
        {
            throw std::system_error(errno, std::system_category(), FILELINE);
        }

        bufsz = 0;
        optlen = sizeof(bufsz);
        result = getsockopt(sockets[i], SOL_SOCKET, SO_RCVBUF, (char *)&bufsz, &optlen);
        if (result == -1)
        {
            throw std::system_error(errno, std::system_category(), FILELINE);
        }
        if (worker_nr == 0 && i == 0) logdebug << "Got rmem buffer size: " << bufsz << '\n';

        set_nonblocking(sockets[i]);
        raddr.sin_port = htons(start_port + i);
        result = bind(sockets[i], (sockaddr *)&raddr, sizeof(raddr));
    }

    // Prepare for using epoll.
    epollfd = epoll_create1(0);
    if (epollfd == -1)
    {
        throw std::system_error(errno, std::system_category(), FILELINE);
    }

    memset(&ev, 0, sizeof(ev));
    for (int i = 0; i < nr_streams; i++)
    {
        ev.events = EPOLLIN | EPOLLET;
        ev.data.fd = sockets[i];
        if (epoll_ctl(epollfd, EPOLL_CTL_ADD, sockets[i], &ev) == -1)
        {
            throw std::system_error(errno, std::system_category(), FILELINE);
        }
    }

    for (;;)
    {
        nfds = epoll_pwait(epollfd, events, nr_streams, -1, nullptr);
        if (nfds == -1)
        {
            throw std::system_error(errno, std::system_category(), FILELINE);
        }
        for (int i = 0; i < nfds; i++)
        {

            int pktcount_onesock = 0;
            for (;;) // receive all on socket
            {
                //logdebug << "events[i].data.fd " << events[i].data.fd << ", ";
                recv_bytes = recv(events[i].data.fd, buf, sizeof(buf), 0);
                if (recv_bytes == -1)
                {
                    if (errno == EAGAIN || errno == EWOULDBLOCK)
                    {
                        break;
                    }
                    else
                    {
                        throw std::system_error(errno, std::system_category(), FILELINE);
                    }
                }
                else
                {
                    pktcount_onesock++;
                    *(byte_count+worker_nr) += recv_bytes;
                    (*(pkt_count+worker_nr))++;
                }
            }
        }
    }

    for (int i = 0; i < nr_streams; i++)
    {
        result = close(sockets[i]);
        if (result == -1)
        {
            throw std::system_error(errno, std::system_category(), FILELINE);
        }
    }
    close(epollfd);

    return 0;
}

int main(int argc, char *argv[])
{
    string receiver_ip;
    in_port_t start_port;
    int nr_streams;
    int nr_workers;
    vector<thread> threads;

    uint64_t total_bytes = 0;
    uint64_t total_pkts = 0;

    if (argc != 5)
    {
        cout << "Usage: receiver <dest ip> <dest start of port range> <nr streams> <nr worker threads>\n";
    }

    receiver_ip = string(argv[1]);
    start_port = stoi(argv[2]);
    nr_streams = stoi(argv[3]);
    nr_workers = stoi(argv[4]);

    int streams_per_worker = nr_streams / nr_workers;

    assert(streams_per_worker > 0);

    pkt_count = new atomic<uint64_t>[nr_workers];
    byte_count = new atomic<uint64_t>[nr_workers];

    INIT_LOGGING("/tmp/manysocklog.txt", LOG_DEBUG);
    cout << "Starting. Using " << nr_workers << " worker threads and " << nr_streams << " streams.\n";

    for (int i = 0; i < nr_workers; i++)
    {
        threads.push_back(thread(receiver_thread,
                                 receiver_ip,
                                 start_port + i*streams_per_worker,
                                 streams_per_worker,
                                 i));
    }

    int seconds = 0;
    const int INTERVAL = 10;
    for (;;)
    {
        total_bytes = 0;
        total_pkts = 0;
        sleep(INTERVAL);
        seconds += INTERVAL;
        for (int i = 0; i < nr_workers; i++)
        {
            total_bytes += (byte_count+i)->exchange(0);
            total_pkts += (pkt_count+i)->exchange(0);
        }
        cout << "Second " << seconds << ": nr recv pkts " << (double)total_pkts << ", nr bytes "
             << (double)total_bytes
             << ", pkts/s " << ((double)total_pkts)/INTERVAL << ", (goodput) bits/s " << ((double)total_bytes*8/INTERVAL)
             <<'\n';

    }

    return 0;
}
