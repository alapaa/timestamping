#include <stdexcept>
#include <string>

//#include <streambuf>
#include <system_error>
#include <memory>

#include <cassert>
#include <unistd.h>
#include <netinet/in.h>
#include <linux/net_tstamp.h>

#include "logging.h"
#include "util.h"
#include "packet.h"
#include "sender.h"

using std::stoi;
using std::stod;
using std::string;
using std::shared_ptr;

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
    double send_interval;

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
    timespec initial_time {0, 0};
    int result;

    INIT_LOGGING("/tmp/tslog.txt", LOG_DEBUG);
    try
    {
        if (argc != 7)
        {
            throw std::runtime_error("Usage: sender <ip addr> <port> <ip ver (4 or 6)> <nr of packets> <iface> <send interval [ms]>");
        }
        else
        {
            address = string(argv[1]);
            port = stoi(argv[2]);
            ipver = stoi(argv[3]);
            domain = ipver == 6 ? AF_INET6 : AF_INET;
            nr_packets = stoi(argv[4]);
            iface_name = string(argv[5]);
            send_interval = stod(argv[6]);
        }

        const long long int INTERVAL_NANOSEC = send_interval * 1000000;
        sock = setup_socket(domain, SOCK_DGRAM,
                            SOF_TIMESTAMPING_TX_HARDWARE |
                            SOF_TIMESTAMPING_RX_HARDWARE |
                            SOF_TIMESTAMPING_RAW_HARDWARE |
                            SOF_TIMESTAMPING_TX_SOFTWARE |
                            SOF_TIMESTAMPING_RX_SOFTWARE |
                            SOF_TIMESTAMPING_SOFTWARE);
        setup_device(sock, iface_name, SOF_TIMESTAMPING_TX_HARDWARE | SOF_TIMESTAMPING_RX_HARDWARE);

        uint32_t send_counter = 0;
        logdebug << "Entering send loop.\n";
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
                logdebug << "----------No data from sender at lap " << (send_counter - 1) << '\n';
                continue;
            }
            shared_ptr<ReflectorPacket> rp = deserialize_reflector_packet(data, datalen);
            if (rp->sender_seq != (send_counter-1))
            {
                logdebug << "---------- REFLECTED SEQNR " << rp->sender_seq << "DOES NOT MATCH SEND SEQNR " <<
                    (send_counter-1) << '\n';
            }
            t2_prev = make_timespec(rp->t2_sec, rp->t2_nsec);
            t3_prev = make_timespec(rp->t3_sec, rp->t3_nsec);

            logdebug << t1_prev << '\n';
            logdebug << t2_prev << '\n';
            logdebug << t3_prev << '\n';
            logdebug << t4_prev << '\n';

            if (t1_prev != ZERO_TS && t4_prev != ZERO_TS &&
                t2_prev != ZERO_TS && t3_prev != ZERO_TS)
            {
                if (initial_clock_diff == ZERO_TS)
                {
                    initial_clock_diff = subtract_ts(t1_prev, t2_prev);
                    initial_time = t1_prev;
                    logdebug << "Setting initial clock diff: ";
                    logdebug << initial_clock_diff;
                    logdebug << '\n';
                }
                else
                {
                    logdebug << "Showing initial clock diff: ";
                    logdebug << initial_clock_diff;
                    logdebug << '\n';

                    logdebug << "Clock diff sender NIC clock <->reflector NIC clock: ";
                    timespec clockdiff = subtract_ts(t1_prev, t2_prev);
                    logdebug << clockdiff << '\n';
                    loginfo << "Clock drift since start: ";
                    timespec drift = subtract_ts(clockdiff, initial_clock_diff);
                    timespec time_elapsed = subtract_ts(t1_prev, initial_time);
                    loginfo << drift << '\n';
                    loginfo << "Clock drift (sender<->receiver NIC) as ppm: " << 1E6 * (drift.tv_sec + drift.tv_nsec / 1E9) /
                        (time_elapsed.tv_sec + time_elapsed.tv_nsec / 1E9) << '\n';
                }

                rtt_soft = subtract_ts(t4_prev, t1_prev);
                delay_on_refl = subtract_ts(t3_prev, t2_prev);
                rtt_hard = subtract_ts(rtt_soft, delay_on_refl);

                if (send_counter % 1 == 0)
                {
                    loginfo << "rtt soft: ";
                    loginfo << rtt_soft << '\n';
                    loginfo << "rtt hard: ";
                    loginfo << rtt_hard << '\n';
                }
            }
            else
            {
                loginfo << "---- missing values in 4-tuple T1 - T4, cannot compute\n";
            }

            logdebug << "Sleeping...\n";
            timespec currtime;
            clock_gettime(CLOCK_MONOTONIC, &currtime);
            int remain_sleeplen_ns = INTERVAL_NANOSEC - (currtime.tv_nsec % INTERVAL_NANOSEC);
            //logdebug << "sleeplen " << remain_sleeplen_ns << '\n';
            result = usleep(remain_sleeplen_ns/1000);
            if (result == -1)
            {
                throw std::system_error(errno, std::system_category());
            }
        }
    }
    catch (std::exception &exc)
    {
        logerr << "Got exception: " << exc.what() << '\n';
        exit(1);
    }

    return 0;
}
