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
//atomic<uint64_t> *eagain_count;

int sender_thread(string receiver_ip, in_port_t start_port, int nr_streams, const int worker_nr, int bufsz, int sleeplen)
{
    int sent_bytes;
    const size_t PKT_PAYLOAD = 32;
    char buf[PKT_PAYLOAD];
    int result;
    int tmp;
    socklen_t optlen;

    vector<int> sockets(nr_streams);
    sockaddr_storage receiver_addr;
    create_sockaddr_storage(AF_INET, receiver_ip, start_port, &receiver_addr);

    sockaddr_in raddr = *reinterpret_cast<sockaddr_in *>(&receiver_addr);

    int nfds;
    struct epoll_event ev;
    struct epoll_event events[nr_streams];
    int epollfd;

    for (int i = 0; i < nr_streams; i++)
    {
        sockets[i] = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (sockets[i] == -1)
        {
            throw std::system_error(errno, std::system_category(), FILELINE);
        }
        raddr.sin_port = ntohs(start_port + i);
        result = connect(sockets[i], (sockaddr *)&raddr, sizeof(raddr));
        if (result == -1)
        {
            throw std::system_error(errno, std::system_category(), FILELINE);
        }
        set_nonblocking(sockets[i]);
        result = setsockopt(sockets[i], SOL_SOCKET, SO_SNDBUF, (const char *)&bufsz, sizeof(bufsz));
        if (result == -1)
        {
            throw std::system_error(errno, std::system_category(), FILELINE);
        }

        bufsz = 0;
        optlen = sizeof(bufsz);
        result = getsockopt(sockets[i], SOL_SOCKET, SO_SNDBUF, (char *)&bufsz, &optlen);
        if (result == -1)
        {
            throw std::system_error(errno, std::system_category(), FILELINE);
        }
        if (worker_nr == 0 && i == 0) logdebug << "Got buffer size " << bufsz << '\n';
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
        ev.events = EPOLLOUT | EPOLLET;
        ev.data.fd = sockets[i];
        if (epoll_ctl(epollfd, EPOLL_CTL_ADD, sockets[i], &ev) == -1)
        {
            throw std::system_error(errno, std::system_category(), FILELINE);
        }
    }

    // Prepare for sendmmsg
    const size_t NR_MSGS = 20;
    struct mmsghdr msg[NR_MSGS];
    struct iovec msg1;
    memset(&msg1, 0, sizeof(msg1));
    msg1.iov_base = buf;
    msg1.iov_len = PKT_PAYLOAD;

    memset(msg, 0, sizeof(msg));
    for (int i = 0; i < NR_MSGS; i++)
    {
        msg[i].msg_hdr.msg_iov = &msg1;
        msg[i].msg_hdr.msg_iovlen = 1;
    }
    // The send loop.
    for (;;)
    {
        //usleep(sleeplen);
        nfds = epoll_pwait(epollfd, events, nr_streams, -1, nullptr);
        if (nfds == -1)
        {
            throw std::system_error(errno, std::system_category(), FILELINE);
        }
        for (int i = 0; i < nfds; i++)
        {
            sent_bytes = 0;
            result = sendmmsg(events[i].data.fd, msg, NR_MSGS, 0);
            if (result == -1)
            {
                throw std::system_error(errno, std::system_category(), FILELINE);
            }
            else
            {
                //logdebug << "sendmmsg sent pkts: " << result << '\n';
                for (int j = 0; j < result; j++)
                {
                    sent_bytes += msg[j].msg_len;
                }
            }

            *(byte_count+worker_nr) += sent_bytes;
            (*(pkt_count+worker_nr))+= result;
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

    return 0;
}

int main(int argc, char *argv[])
{
    string receiver_ip;
    in_port_t start_port;
    int nr_streams;
    int nr_workers;
    int bufsz;
    int sleeplen;
    vector<thread> threads;

    uint64_t total_bytes = 0;
    uint64_t total_pkts = 0;
    uint64_t total_eagain = 0;

    if (argc != 7)
    {
        cout << "Usage: sender <dest ip> <dest start of port range> <nr streams> <nr worker threads> <wmem_sz [kB]> <sleeplen us>\n";
        exit(1);
    }

    INIT_LOGGING("/tmp/manysocklog.txt", LOG_DEBUG);
    receiver_ip = string(argv[1]);
    start_port = stoi(argv[2]);
    nr_streams = stoi(argv[3]);
    nr_workers = stoi(argv[4]);
    bufsz = stoi(argv[5]);
    sleeplen = stoi(argv[6]);
    int streams_per_worker = nr_streams / nr_workers;

    assert(streams_per_worker > 0);

    pkt_count = new atomic<uint64_t>[nr_workers];
    byte_count = new atomic<uint64_t>[nr_workers];
    //eagain_count = new atomic<uint64_t>[nr_workers];

    cout << "Starting. Using " << nr_workers << " worker threads and " << nr_streams << " streams.\n";

    for (int i = 0; i < nr_workers; i++)
    {
        threads.push_back(thread(sender_thread,
                                 receiver_ip,
                                 start_port + i*streams_per_worker,
                                 streams_per_worker,
                                 i,
                                 bufsz*1024,
                                 sleeplen));
    }

    int seconds = 0;
    long int sndbuf_errors = 0;
    long int sndbuf_prev_errors = 0;
    for (;;)
    {
        sleep(1);
        seconds++;
        for (int i = 0; i < nr_workers; i++)
        {
            total_bytes += (byte_count+i)->exchange(0);
            total_pkts += (pkt_count+i)->exchange(0);
            //total_eagain += (eagain_count+i)->exchange(0);
        }
        if (seconds % 10 == 0)
        {
            loginfo << "Second " << seconds << ": nr pkts " << (double)total_pkts << ", nr bytes "
                 << (double)total_bytes
                 << ", pkts/sec " << ((double)total_pkts)/seconds << ", (goodput) bits/s " << ((double)total_bytes*8/seconds)
                //<< ", drop_count " << total_eagain
                 << '\n';
            sndbuf_errors = stol(exec("/bin/netstat -s | grep -i sndbuf | cut -d\':\' -f2"));
            loginfo  << "Nr of netstat sndbuf errors: " << sndbuf_errors - sndbuf_prev_errors << '\n';
            sndbuf_prev_errors = sndbuf_errors;
        }

    }

    delete[] pkt_count;
    delete[] byte_count;
    //delete[] eagain_count;

    return 0;
}
