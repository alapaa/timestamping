#include <iostream>
#include <thread>
#include <vector>
#include <cassert>
#include <system_error>
#include <atomic>
#include <cstdint>
#include <unistd.h>

#include "util.h"

using std::thread;
using std::vector;
using std::cout;
using std::system_error;
using std::atomic;
using std::string;
using std::stoi;
using std::to_string;

atomic<uint64_t> *byte_count;
atomic<uint64_t> *pkt_count;

int sender_thread(string receiver_ip, in_port_t start_port, int nr_streams, const int worker_nr)
{
    int sent_bytes;
    const size_t PKT_PAYLOAD = 32;
    char buf[PKT_PAYLOAD];
    int result;
    int tmp;

    vector<int> sockets(nr_streams);
    sockaddr_storage receiver_addr;
    create_sockaddr_storage(AF_INET, receiver_ip, start_port, &receiver_addr);

    sockaddr_in raddr = *reinterpret_cast<sockaddr_in *>(&receiver_addr);

    // cout << "Worker " << worker_nr << " starting on CPU " << sched_getcpu() << '\n';

    for (int i = 0; i < nr_streams; i++)
    {
        sockets[i] = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (sockets[i] == -1)
        {
            throw std::system_error(errno, std::system_category(), FILELINE);
        }
    }

    for (;;)
    {
        for (int i = 0; i < nr_streams; i++)
        {
            raddr.sin_port = ntohs(start_port + i);
            sent_bytes = sendto(sockets[i], buf, PKT_PAYLOAD, 0, (sockaddr *)&raddr, sizeof(raddr));
            if (sent_bytes == -1)
            {
                throw std::system_error(errno, std::system_category(), FILELINE);
            }
            *(byte_count+worker_nr) += sent_bytes;
            (*(pkt_count+worker_nr))++;
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
    vector<thread> threads;

    uint64_t total_bytes = 0;
    uint64_t total_pkts = 0;

    if (argc != 5)
    {
        cout << "Usage: sender <dest ip> <dest start of port range> <nr streams> <nr worker threads>\n";
    }

    receiver_ip = string(argv[1]);
    start_port = stoi(argv[2]);
    nr_streams = stoi(argv[3]);
    nr_workers = stoi(argv[4]);

    int streams_per_worker = nr_streams / nr_workers;

    assert(streams_per_worker > 0);

    pkt_count = new atomic<uint64_t>[nr_workers];
    byte_count = new atomic<uint64_t>[nr_workers];

    cout << "Starting. Using " << nr_workers << " worker threads and " << nr_streams << " streams.\n";

    for (int i = 0; i < nr_workers; i++)
    {
        threads.push_back(thread(sender_thread,
                                 receiver_ip,
                                 start_port + i*streams_per_worker,
                                 streams_per_worker,
                                 i));
    }

    int seconds = 0;
    for (;;)
    {
        sleep(1);
        seconds++;
        for (int i = 0; i < nr_workers; i++)
        {
            total_bytes += (byte_count+i)->exchange(0);
            total_pkts += (pkt_count+i)->exchange(0);
        }
        if (seconds % 10 == 0)
        {
            cout << "Second " << seconds << ": nr pkts " << (double)total_pkts << ", nr bytes " << total_bytes <<
            ", pkts/sec " << ((double)total_pkts)/seconds << '\n';
        }

    }

    return 0;
}
