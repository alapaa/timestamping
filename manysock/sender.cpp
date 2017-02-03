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
#include <queue>

#include <sys/epoll.h>

#include "util.h"
#include "packet.h"
#include "logging.h"

using std::thread;
using std::vector;
using std::map;
using std::priority_queue;
using std::pair;
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

const size_t NR_MSGS = 5;
const int MEGABIT2BIT = 1048576;
struct stream
{
    int id;
    int sock;
    double rate; // Unit [bits/s]
    size_t packet_size;
    timespec next_send;
};

const int MILLION = 1000000;

double compute_next_send(stream s)
{
    return NR_MSGS*s.packet_size*8/s.rate;
}

bool operator>(const pair<timespec, stream>& p1, const pair<timespec, stream>& p2)
{
    const timespec& t1 = p1.first;
    const timespec& t2 = p2.first;
    //assert (abs(t1.tv_nsec) < BILLION && abs(t2.tv_nsec) < BILLION);
    if (t1.tv_sec > t2.tv_sec) return true;
    if (t1.tv_sec == t2.tv_sec && t1.tv_nsec > t2.tv_nsec) return true;
    if (t1 == t2 && p1.second.id > p2.second.id) return true;
    return false;
}

/*
 *
 * Implementation of sender worker using send queue instead of e.g. token bucket.
 */
int sender_thread(string receiver_ip, in_port_t start_port, int nr_streams, int worker_nr, int bufsz, int
 payload_sz, double rate)
{
    int sent_bytes;
    char buf[payload_sz];
    int result;
    int tmp;
    socklen_t optlen;

    vector<int> sockets(nr_streams);
    sockaddr_storage receiver_addr;
    create_sockaddr_storage(AF_INET, receiver_ip, start_port, &receiver_addr);

    sockaddr_in raddr = *reinterpret_cast<sockaddr_in *>(&receiver_addr);

    timespec currtime = {0, 0};

    stream s;
    s.rate = rate * MEGABIT2BIT; // Convert
    s.packet_size = HDR_SZ + payload_sz;

    logdebug << "Size of stream: " << sizeof(stream) << ", size of timespec: " << sizeof(timespec) << '\n';

    std::map<int, stream> streams; // Maps from stream id to stream struct
    try
    {
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

    logdebug << "nr streams in map: " << streams.size() << '\n';

    // Prepare for sendmmsg
    struct mmsghdr msg[NR_MSGS];
    struct iovec msg1;
    memset(&msg1, 0, sizeof(msg1));
    msg1.iov_base = buf;
    msg1.iov_len = payload_sz;

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
    priority_queue<pair<timespec, stream>,
                   std::vector<pair<timespec, stream>>,
                   std::greater<pair<timespec, stream>> > send_queue;

    clock_gettime(CLOCK_MONOTONIC, &currtime);
    timespec tmp_next_send;
    for (auto s: streams)
    {
        double next_send_dbl = compute_next_send(s.second);
        logdebug << "next_send_dbl: " << next_send_dbl << '\n';
        dbl2ts(next_send_dbl, s.second.next_send);
        tmp_next_send = add_ts(currtime, s.second.next_send);
        send_queue.push(make_pair(tmp_next_send, s.second));
        logdebug << "Stream rate: " << s.second.rate << '\n';
    }
    logdebug << "Size of send queue " << send_queue.size() << '\n';

    // The sendqueue-based send loop
    timespec next_send;
    for (;;)
    {
        auto elem = send_queue.top();
        send_queue.pop();
        //logdebug << " " << elem.second.id << '\n';

        next_send = add_ts(elem.first, elem.second.next_send);
        send_queue.push(make_pair(next_send, elem.second));

        clock_gettime(CLOCK_MONOTONIC, &currtime);
        timespec tdiff = subtract_ts(elem.first, currtime);
        // cout << "Currtime " << currtime << ", elem " << elem.first << '\n';
        // if (elem.first < currtime)
        //     logdebug << "Falling behind!\n";
        if (tdiff.tv_sec > 0 || tdiff.tv_sec == 0 && tdiff.tv_nsec > 1000)
        {
            if (tdiff.tv_nsec > 100000) tdiff.tv_nsec -= 100000; // Shorten sleeep somewhat to compensate for send
                                                                 // overhead
            result = nanosleep(&tdiff, nullptr);
            if (result == -1)
            {
                if (errno != EINTR)
                {
                    throw std::system_error(errno, std::system_category(), FILELINE);
                }
            }
        }

        sent_bytes = 0;
        result = sendmmsg(elem.second.sock, msg, NR_MSGS, 0);
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

    }

    }
    catch (std::runtime_error& exc)
    {
        logerr << "Got exception " << exc.what() << '\n';
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
    int payload_sz;
    double rate;
    vector<thread> threads;

    uint64_t total_bytes = 0;
    uint64_t total_pkts = 0;
    uint64_t total_eagain = 0;

    if (argc != 8)
    {
        cout << "Usage: sender <dest ip> <dest start of port range> <nr streams> <nr worker threads> <wmem_sz [kB]> <payload_sz> <rate [Mbit/s]>\n";
        exit(1);
    }

    INIT_LOGGING("/tmp/manysocklog.txt", LOG_DEBUG);
    receiver_ip = string(argv[1]);
    start_port = stoi(argv[2]);
    nr_streams = stoi(argv[3]);
    nr_workers = stoi(argv[4]);
    bufsz = stoi(argv[5]);
    payload_sz = stoi(argv[6]);
    rate = stod(argv[7]);
    int streams_per_worker = nr_streams / nr_workers;

    assert(streams_per_worker > 0);

    const size_t FRAME_SZ = HDR_SZ + payload_sz;
    const double TP_OVER_GP = (double)FRAME_SZ/payload_sz;

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
                                 payload_sz,
                                 rate));
    }

    int seconds = 0;
    const int INTERVAL_LENGTH = 10;
    long int sndbuf_errors = 0;
    long int sndbuf_prev_errors = 0;
    int execution_time = 25;
    for (;;)
    {
        sleep(INTERVAL_LENGTH);
        execution_time -= INTERVAL_LENGTH;
        if (execution_time < 0)
            break;
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
        string output = exec("/bin/netstat -s | grep -i sndbuf | cut -d\':\' -f2");
        if (output.size())
        {
            sndbuf_errors = stol(output);
        }
        else
        {
            sndbuf_errors = 0;
        }
        loginfo  << "Nr of netstat sndbuf errors: " << sndbuf_errors - sndbuf_prev_errors << '\n';
        sndbuf_prev_errors = sndbuf_errors;
    }

    for (auto& t: threads)
    {
        t.join();
    }

    delete[] pkt_count;
    delete[] byte_count;
    //delete[] eagain_count;

    return 0;
}
