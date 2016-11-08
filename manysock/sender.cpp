#include <iostream>
#include <thread>
#include <vector>
#include <unordered_map>
#include <cassert>
#include <system_error>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <unistd.h>

#include <sys/epoll.h>

#include "util.h"
#include "packet.h"
#include "logging.h"

using std::thread;
using std::vector;
using std::unordered_map;
using std::cout;
using std::system_error;
using std::atomic;
using std::string;
using std::stoi;
using std::stod;
using std::to_string;
using std::min;

using namespace Netrounds;

atomic<uint64_t> *byte_count;
atomic<uint64_t> *pkt_count;
//atomic<uint64_t> *eagain_count;


struct bucket_data
{
    int b; // Bucket size in bytes.
    int r; // Bucket rate, bytes/s.
    int n_tokens; // Current nr of tokens in bucket, in bytes.
    bool in_epoll;
};

void replenish_buckets(vector<bucket_data>& bucket, const timespec& prevtime, const timespec& currtime)
{
    timespec tdiff = subtract_ts(currtime, prevtime);
    double tdiff_dbl = tdiff.tv_sec + tdiff.tv_nsec/1E9;
    //cout << "tdiff" << tdiff_dbl << '\n';
    for (auto &buck: bucket)
    {
        //cout << "Adding " << buck.r * tdiff_dbl << "tokens\n";
        buck.n_tokens += buck.r * tdiff_dbl;
        //cout << "n_tokens after replenish: " << buck.n_tokens << '\n';
        buck.n_tokens = min(buck.n_tokens, buck.b);
        //cout << "after cap: " << buck.n_tokens << '\n';
    }
}

/*
 *
 * Each sender thread uses a token bucket to control packet rate for each individual stream.
 *
 * From Wikipedia (r is rate in bytes/s):
 * The token bucket algorithm can be conceptually understood as follows:
 * - A token is added to the bucket every 1/r seconds.
 * - The bucket can hold at the most b tokens. If a token arrives when the bucket is full, it is discarded.
 * - When a packet (network layer PDU) of n bytes arrives, n tokens are removed from the bucket, and the packet is sent
 *   to the network.
 * - If fewer than n tokens are available, no tokens are removed from the bucket, and the packet is considered to be
 *   non-conformant.
 *
 * We drop non-conformant packets.
 *
 */

int sender_thread(string receiver_ip, in_port_t start_port, int nr_streams, const int worker_nr, int bufsz, double rate)
{
    int sent_bytes;
    const size_t PKT_PAYLOAD = 32;
    const size_t HDR_SZ = 18+20+8; // TODO: IPv4 hardcoded
    const size_t FRAME_SZ = HDR_SZ + PKT_PAYLOAD;
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

    timespec currtime = {0, 0};
    timespec prevtime = {0, 0};
    vector<bucket_data> bucket(nr_streams);
    unordered_map<int, int> sock2stream;

    for (int i = 0; i < nr_streams; i++)
    {
        sockets[i] = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        sock2stream.insert(std::make_pair(sockets[i], i));
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

    // Prepare for token bucket
    for (auto& buck : bucket)
    {
        buck.r = rate * 1024 * 1024 / 8;
        buck.b = buck.r;
        buck.n_tokens = buck.b/2;
        //cout << buck.r << ' '<< buck.b << ' ' << buck.n_tokens << '\n';
    }
    //cout << "Initialized bucket 0 to " << bucket[0].r << ' ' << bucket[0].b << ' ' << bucket[0].n_tokens << '\n';

    // Initialize counters

    *(byte_count+worker_nr) = 0;
    (*(pkt_count+worker_nr)) = 0;

    // The send loop.
    clock_gettime(CLOCK_MONOTONIC, &currtime);
    for (;;)
    {
        prevtime = currtime;
        clock_gettime(CLOCK_MONOTONIC, &currtime);
        replenish_buckets(bucket, prevtime, currtime);
        for (int i = 0; i < nr_streams; i++)
        {
            ev.events = EPOLLOUT | EPOLLET;
            ev.data.fd = sockets[i];
            if (bucket[i].n_tokens >= FRAME_SZ*NR_MSGS && bucket[i].in_epoll == false)
            {
                if (epoll_ctl(epollfd, EPOLL_CTL_ADD, sockets[i], &ev) == -1)
                {
                    throw std::system_error(errno, std::system_category(), FILELINE);
                }
                bucket[i].in_epoll = true;
            }
            else if (bucket[i].n_tokens < FRAME_SZ*NR_MSGS && bucket[i].in_epoll == true)
            {
                if (epoll_ctl(epollfd, EPOLL_CTL_DEL, sockets[i], nullptr) == -1)
                {
                    throw std::system_error(errno, std::system_category(), FILELINE);
                }
                bucket[i].in_epoll = false;
            }
        }
        nfds = epoll_pwait(epollfd, events, nr_streams, 1000, nullptr);
        if (nfds == -1)
        {
            throw std::system_error(errno, std::system_category(), FILELINE);
        }
        for (int i = 0; i < nfds; i++)
        {
            sent_bytes = 0;
            int sid = sock2stream.at(events[i].data.fd);
            //cout << "sid " << sid << "bucket " << bucket.at(sid).n_tokens << '\n';
            assert (bucket[sid].n_tokens >= FRAME_SZ*NR_MSGS);

            result = sendmmsg(events[i].data.fd, msg, NR_MSGS, 0);
            if (result == -1)
            {
                throw std::system_error(errno, std::system_category(), FILELINE);
            }
            else
            {
                for (int j = 0; j < result; j++)
                {
                    sent_bytes += msg[j].msg_len;
                }
                //cout << "Removing " << sent_bytes << "tokens from bucket " << sid << '\n';
                bucket[sid].n_tokens -= (sent_bytes + result * HDR_SZ);

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
    double rate;
    vector<thread> threads;

    uint64_t total_bytes = 0;
    uint64_t total_pkts = 0;
    uint64_t total_eagain = 0;

    if (argc != 7)
    {
        cout << "Usage: sender <dest ip> <dest start of port range> <nr streams> <nr worker threads> <wmem_sz [kB]> <rate [Mbit/s]>\n";
        exit(1);
    }

    INIT_LOGGING("/tmp/manysocklog.txt", LOG_DEBUG);
    receiver_ip = string(argv[1]);
    start_port = stoi(argv[2]);
    nr_streams = stoi(argv[3]);
    nr_workers = stoi(argv[4]);
    bufsz = stoi(argv[5]);
    rate = stod(argv[6]);
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
                                 rate));
    }

    int seconds = 0;
    const int INTERVAL_LENGTH = 10;
    long int sndbuf_errors = 0;
    long int sndbuf_prev_errors = 0;
    for (;;)
    {
        sleep(INTERVAL_LENGTH);
        seconds += INTERVAL_LENGTH;
        for (int i = 0; i < nr_workers; i++)
        {
            total_bytes += (byte_count+i)->exchange(0);
            total_pkts += (pkt_count+i)->exchange(0);
            //total_eagain += (eagain_count+i)->exchange(0);
        }

        loginfo << "Second " << seconds << ": nr pkts " << (double)total_pkts << ", nr bytes "
                << (double)total_bytes
                << ", pkts/sec " << ((double)total_pkts)/INTERVAL_LENGTH << ", (goodput) bits/s " << ((double)total_bytes*8/INTERVAL_LENGTH)
            //<< ", drop_count " << total_eagain
                << '\n';
        total_bytes = 0;
        total_pkts = 0;
        sndbuf_errors = stol(exec("/bin/netstat -s | grep -i sndbuf | cut -d\':\' -f2"));
        loginfo  << "Nr of netstat sndbuf errors: " << sndbuf_errors - sndbuf_prev_errors << '\n';
        sndbuf_prev_errors = sndbuf_errors;


    }

    delete[] pkt_count;
    delete[] byte_count;
    //delete[] eagain_count;

    return 0;
}
