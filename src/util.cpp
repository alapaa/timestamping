#include <iostream>
#include <system_error>

#include <cstdio>
#include <cstdlib>
#include <errno.h>
#include <cstring>

#include <fcntl.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <asm/types.h>

#include <linux/sockios.h>
#include <linux/net_tstamp.h>
#include <linux/errqueue.h>

#include "logging.h"
#include "util.h"
#include "gpl_code_remove.h"

using std::cout;
using std::tuple;
using std::shared_ptr;
using std::string;

using namespace Netrounds;

std::string exec(const char* cmd) {
    char buffer[128];
    std::string result = "";
    std::shared_ptr<FILE> pipe(popen(cmd, "r"), pclose);
    if (!pipe) throw std::runtime_error("popen() failed!");
    while (!feof(pipe.get())) {
        if (fgets(buffer, 128, pipe.get()) != NULL)
            result += buffer;
    }
    return result;
}

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

void do_bind(int sock, sockaddr_storage *ss)
{
    int result = bind(sock, (sockaddr *)ss, sizeof(*ss));
    if (result == -1)
    {
        throw std::system_error(errno, std::system_category());
    }
}

void set_nonblocking(int sock)
{
    int nonblock = 1;
    if (fcntl(sock, F_SETFL, O_NONBLOCK, nonblock) == -1)
    {
        throw std::system_error(errno, std::system_category());
    }
}

void setup_device(int sock, string iface_name, int so_timestamping_flags)
{
    int result;

    struct ifreq hwtstamp;
    struct hwtstamp_config hwconfig, hwconfig_requested;

    memset(&hwtstamp, 0, sizeof(hwtstamp));
    strncpy(hwtstamp.ifr_name, iface_name.c_str(), sizeof(hwtstamp.ifr_name));
    hwtstamp.ifr_data = reinterpret_cast<char *>(&hwconfig);
    memset(&hwconfig, 0, sizeof(hwconfig));
    hwconfig.tx_type =
        (so_timestamping_flags & SOF_TIMESTAMPING_TX_HARDWARE) ?
        HWTSTAMP_TX_ON : HWTSTAMP_TX_OFF;
    hwconfig.rx_filter =
        (so_timestamping_flags & SOF_TIMESTAMPING_RX_HARDWARE) ?
        HWTSTAMP_FILTER_PTP_V1_L4_SYNC : HWTSTAMP_FILTER_NONE;
    hwconfig_requested = hwconfig;
    result = ioctl(sock, SIOCSHWTSTAMP, &hwtstamp);
    if (result == -1)
    {
        if ((errno == EINVAL || errno == ENOTSUP) &&
            hwconfig_requested.tx_type == HWTSTAMP_TX_OFF &&
            hwconfig_requested.rx_filter == HWTSTAMP_FILTER_NONE)
        {
            logdebug << "SIOCSHWTSTAMP: disabling hardware time stamping not possible\n";
        }
        else
        {
            throw std::system_error(errno, std::system_category());
        }
    }
    logdebug << "SIOCSHWTSTAMP: tx_type " << hwconfig_requested.tx_type << " requested, got " << hwconfig.tx_type <<
        "; rx_filter " << hwconfig_requested.rx_filter << " requested, got " << hwconfig.rx_filter << '\n';
}

int setup_socket(int domain, int type, int so_timestamping_flags)
{
    int val;
    socklen_t len;

    if (type != SOCK_DGRAM)
    {
        throw std::runtime_error("Only SOCK_DGRAM supported so far...");
    }
    int sock = socket(domain, type, IPPROTO_UDP);
    if (sock == -1)
    {
        throw std::system_error(errno, std::system_category());
    }

    // Request timestamping
    loginfo << "Setting SO_TIMESTAMPING flags on socket... ";
    int result = setsockopt(sock, SOL_SOCKET, SO_TIMESTAMPING, (void *) &so_timestamping_flags,
                            sizeof(so_timestamping_flags));
    if (result == -1)
    {
        logerr << "Failed!\n";
        throw std::system_error(errno, std::system_category());
    }
    loginfo << "OK!\n";

    loginfo << "Checking SO_TIMESTAMPING flags... ";
    len = sizeof(val);
    result = getsockopt(sock, SOL_SOCKET, SO_TIMESTAMPING, &val, &len);
    loginfo << "Got result " << result << '\n';
    if (result == -1)
    {
        throw std::system_error(errno, std::system_category());
    }
    else
    {
        logdebug << "SO_TIMESTAMPING " << val << '\n';
        if (val != so_timestamping_flags)
        {
            loginfo << "Not the expected value of SO_TIMESTAMPING flags: " << so_timestamping_flags;
        }
        else
        {
            loginfo << "Flags OK!\n";
        }
    }

    int enabled = 1;
    result = setsockopt(sock, SOL_SOCKET, SO_TIMESTAMPNS,
                        &enabled, sizeof(enabled));
    if (result == -1)
    {
        throw std::system_error(errno, std::system_category());
    }
    /* request IP_PKTINFO for debugging purposes */

    result = setsockopt(sock, SOL_IP, IP_PKTINFO, &enabled, sizeof(enabled));
    if (result == -1)
    {
        throw std::system_error(errno, std::system_category());
    }

    return sock;
}


void sendpacket(int domain, string address, in_port_t port, int sock, char *buf, size_t buflen)
{
    sockaddr_storage ss;
    create_sockaddr_storage(domain, address, port, &ss);

    logdebug << "Sending, ip addr " << address << " domain " << (domain == AF_INET ? "AF_INET" : "AF_INET6") << '\n';
    sendpacket(&ss, sock, buf, buflen);
}

void sendpacket(sockaddr_storage *ss, int sock, char *buf, size_t buflen)
{
    int result;


#ifdef DEBUG
    char addrstr[INET_ADDRSTRLEN];
    const char *resbuf = inet_ntop(ss->ss_family, &((sockaddr_in *)ss)->sin_addr, addrstr, sizeof(addrstr));
    if (!resbuf)
    {
        throw std::system_error(errno, std::system_category());
    }
    logdebug << "Sending to " << addrstr << '\n';
#endif

    // TODO: Handle EAGAIN better.
    for (;;)
    {
        result = sendto(sock, buf, buflen, 0, (sockaddr *)ss, sizeof(*ss));
        if (result == -1)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                logdebug << "Got EAGAIN/EWOULDBLOCK, doing sleep/retry\n";
                sleep(1);
                continue;
            }
            throw std::system_error(errno, std::system_category());
        }
        logdebug << "Sent " << result << " bytes\n";
        break;
    }
}

tuple<shared_ptr<char>, int, sockaddr_storage, timespec> recvpacket(int sock, int recvmsg_flags, bool use_sw_tstamp)
{
    const size_t MAX_LEN = 1600;
    shared_ptr<char> data(new char[MAX_LEN]); // TODO: Change to vector<char> or maybe shared_array
    sockaddr_storage from_addr;
    msghdr msg;
    iovec entry;
    struct {
        struct cmsghdr cm;
        char control[512];
    } control;
    int len;


    memset(&from_addr, 0, sizeof(from_addr));

    memset(&msg, 0, sizeof(msg));
    msg.msg_iov = &entry;
    msg.msg_iovlen = 1;
    entry.iov_base = data.get();
    entry.iov_len = MAX_LEN;
    msg.msg_name = (caddr_t)&from_addr;
    msg.msg_namelen = sizeof(from_addr);
    msg.msg_control = &control;
    msg.msg_controllen = sizeof(control);

    int retry_count = 0;
    for (;;)
    {
        logdebug << "Doing recvmsg, flags " << recvmsg_flags << '\n';
        len = recvmsg(sock, &msg, recvmsg_flags);
        if (len == -1)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                logdebug << "Got EAGAIN/EWOULDBLOCK, doing sleep/retry\n";
                sleep(1);
                if (retry_count++ < 3)
                {
                    continue;
                }
                else
                {
                    logdebug << "Could not receive on sock, giving up for now...\n";
                    timespec null_ts;
                    null_ts.tv_sec = 0;
                    null_ts.tv_nsec = 0;
                    return tuple<shared_ptr<char>, int, sockaddr_storage, timespec>(shared_ptr<char>(nullptr), 0, from_addr, null_ts);
                }
            }
            throw std::system_error(errno, std::system_category());
        }
        else if (msg.msg_flags & MSG_TRUNC)
        {
            throw std::runtime_error("recvmsg, buffer too small, truncated!");
        }
        else
        {
            timespec hwts;
            printpacket(&msg, len, sock, recvmsg_flags, 0, 0, &hwts, use_sw_tstamp);
            return tuple<shared_ptr<char>, int, sockaddr_storage, timespec>(data, len, from_addr, hwts);
        }
    }
}

tuple<shared_ptr<char>, int, sockaddr_storage, timespec> receive_send_timestamp(int sock, bool use_sw_tstamp)
{
    //shared_ptr<char> data;
    //size_t datalen;
    //sockaddr_storage ss;
    //timespec htws;

    //tie(data, datalen, ss, hwts) = recvpacket(sock, MSG_ERRQUEUE);

    //logdebug << "as char: " << data.get()+42 << '\n';
    // char tmp[1500];
    // const int offset = 42;
    // memcpy(tmp, data.get()+offset, 1500); // TODO: 42 is not divisible by word-length 4 bytes. Can we avoid copy?
    // uint32_t *wp = (uint32_t *)tmp;

    // for (int i = 0; i < 3; i++)
    // {
    //     logdebug << "word " << i << " of packet payload from MSG_ERRQUEUE: " << htonl(*(wp+i)) << '\n';
    // }

    return recvpacket(sock, MSG_ERRQUEUE, use_sw_tstamp);
}

void create_sockaddr_storage(int domain, string address, in_port_t port, sockaddr_storage *ssp)
{
    // Implementation note: Should use type-punning through union in this function to make sure the compiler does
    // not attempt any strict aliasing optimizations.
    int result;
    sockaddr_in *sa_in = 0;
    sockaddr_in6 *sa_in6 = 0;

    char *addr = 0;
    memset(ssp, 0, sizeof(*ssp));
    if (domain == AF_INET) {
        sa_in = reinterpret_cast<sockaddr_in *>(ssp);
        addr = reinterpret_cast<char *>(&sa_in->sin_addr.s_addr);
        sa_in->sin_family = AF_INET;
        sa_in->sin_port = htons(port);
    }
    else
    {
        sa_in6 = reinterpret_cast<sockaddr_in6 *>(ssp);
        addr = reinterpret_cast<char *>(&sa_in6->sin6_addr.s6_addr[0]);
        sa_in6->sin6_family = AF_INET6;
        sa_in6->sin6_port = htons(port);
    }

    result = inet_pton(domain, address.c_str(), addr);
    if (result <= 0) {
        if (result == 0)
        {
            throw std::runtime_error("Not in presentation format!");
        }
        else
        {
            throw std::system_error(errno, std::system_category());
        }
    }
}

void wait_for_errqueue_data(int sock)
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
    ts.tv_sec = 5;
    ts.tv_nsec = 0;

    retval = pselect(sock+1, &rfds, NULL, &efds, &ts, NULL);
    if (retval == -1)
    {
        throw std::system_error(errno, std::system_category());
    }
    else if(retval)
    {
        logdebug << "Data is available now.\n";
    }
    else
    {
        logdebug << "No data within five seconds.\n";
    }
}
