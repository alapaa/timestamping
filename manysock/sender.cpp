#include <iostream>
#include <thread>
#include <vector>
#include <map>
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
using std::map;
using std::make_pair;
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

const size_t PKT_PAYLOAD = 32;
const size_t FRAME_SZ = HDR_SZ + PKT_PAYLOAD;
const double TP_OVER_GP = (double)FRAME_SZ/PKT_PAYLOAD;
const size_t NR_MSGS = 5;

struct stream
{
    int id;
    int sock;
    double rate; // Unit [bits/s]
    size_t packet_size;
};

const int MILLION = 1000000;

double compute_next_send(stream s)
{
    return NR_MSGS*s.packet_size*8/s.rate;
}

/*
 *
 * Implementation of sender worker using send queue instead of e.g. token bucket.
 */
int sender_thread(string receiver_ip, in_port_t start_port, int nr_streams, const int worker_nr, int bufsz, double rate)
{
    int sent_bytes;
    char buf[PKT_PAYLOAD];
    int result;
    int tmp;
    socklen_t optlen;

    vector<int> sockets(nr_streams);
    sockaddr_storage receiver_addr;
    create_sockaddr_storage(AF_INET, receiver_ip, start_port, &receiver_addr);

    sockaddr_in raddr = *reinterpret_cast<sockaddr_in *>(&receiver_addr);

    timespec currtime = {0, 0};

    stream s;
    s.rate = rate;
    s.packet_size = PKT_PAYLOAD;

    std::map<int, stream> streams; // Maps from stream id to stream struct

    for (int i = 0; i < nr_streams; i++)
    {
        sockets[i] = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (sockets[i] == -1)
        {
            throw std::system_error(errno, std::system_category(), FILELINE);
        }
        s.id = i;
        s.sock = sockets[i];
        streams.insert(make_pair(i, s));

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

    // Prepare for sendmmsg
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

    // Initialize counters

    *(byte_count+worker_nr) = 0;
    (*(pkt_count+worker_nr)) = 0;


    // Set up send queue
    map<timespec, stream> send_queue;
    timespec next_send;
    clock_gettime(CLOCK_MONOTONIC, &currtime);
    for (auto s: streams)
    {
        double next_send_dbl = compute_next_send(s.second);
        dbl2ts(next_send_dbl, next_send);
        next_send = add_ts(currtime, next_send);
        send_queue.insert(make_pair(next_send, s.second));
    }

    // The sendqueue-based send loop
    for (;;)
    {
        auto it = send_queue.begin();

        double next_send_dbl = compute_next_send(it->second);
        dbl2ts(next_send_dbl, next_send);
        clock_gettime(CLOCK_MONOTONIC, &currtime);
        next_send = add_ts(currtime, next_send);
        send_queue.insert(make_pair(next_send, it->second));

        clock_gettime(CLOCK_MONOTONIC, &currtime); // TODO: Maybe good enough to reuse gettime from above, saving a
                                                   // clock_gettime call?
        timespec tdiff = subtract_ts(it->first, currtime);
        if (tdiff.tv_sec > 0 || tdiff.tv_nsec > MILLION)
        {
            result = nanosleep(&tdiff, nullptr);
            if (result == -1)
            {
                throw std::system_error(errno, std::system_category(), FILELINE);
            }
        }
        result = sendmmsg(it->second.sock, msg, NR_MSGS, 0);
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
        }
        *(byte_count+worker_nr) += sent_bytes;
        (*(pkt_count+worker_nr))+= result;

        // Prepare for next send
        send_queue.erase(it);
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

    cout << "Starting. Using " << nr_workers << " worker threads and " << streams_per_worker*nr_workers << " streams.\n";

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
                << ", pkts/sec " << ((double)total_pkts)/INTERVAL_LENGTH << ", bits/s " << ((double)total_bytes*8*TP_OVER_GP/INTERVAL_LENGTH)
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
