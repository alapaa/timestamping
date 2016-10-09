#include <iostream>
#include <system_error>

#include <cstdio>
#include <cstdlib>
#include <errno.h>
#include <cstring>

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

#include "util.h"
#include "gpl_code_remove.h"

using std::cout;


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
            cout << "SIOCSHWTSTAMP: disabling hardware time stamping not possible\n";
        }
        else
        {
            throw std::system_error(errno, std::system_category());
        }
    }
    cout << "SIOCSHWTSTAMP: tx_type " << hwconfig_requested.tx_type << " requested, got " << hwconfig.tx_type <<
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
    int result = setsockopt(sock, SOL_SOCKET, SO_TIMESTAMPING, (void *) &so_timestamping_flags,
                            sizeof(so_timestamping_flags));
    if (result == -1)
    {
        throw std::system_error(errno, std::system_category());
    }

    len = sizeof(val);
    result = getsockopt(sock, SOL_SOCKET, SO_TIMESTAMPING, &val, &len);
    if (result == -1)
    {
        throw std::system_error(errno, std::system_category());
    }
    else
    {
        cout << "SO_TIMESTAMPING " << val << '\n';
        if (val != so_timestamping_flags)
        {
            cout << "Not the expected value " << so_timestamping_flags;
        }
    }

    /* request IP_PKTINFO for debugging purposes */
    int enabled = 1;
    result = setsockopt(sock, SOL_IP, IP_PKTINFO, &enabled, sizeof(enabled));
    if (result == -1)
    {
        throw std::system_error(errno, std::system_category());
    }

    return sock;
}

static void recvpacket(int sock, int recvmsg_flags)
{
    char data[256];
    struct msghdr msg;
    struct iovec entry;
    struct sockaddr_in from_addr;
    struct {
        struct cmsghdr cm;
        char control[512];
    } control;
    int res;

    memset(&msg, 0, sizeof(msg));
    msg.msg_iov = &entry;
    msg.msg_iovlen = 1;
    entry.iov_base = data;
    entry.iov_len = sizeof(data);
    msg.msg_name = (caddr_t)&from_addr;
    msg.msg_namelen = sizeof(from_addr);
    msg.msg_control = &control;
    msg.msg_controllen = sizeof(control);

    res = recvmsg(sock, &msg, recvmsg_flags|MSG_DONTWAIT);
    if (res == -1)
    {
        throw std::system_error(errno, std::system_category());
    }
    else
    {
        printpacket(&msg, res, data,
                sock, recvmsg_flags,
                0, 0);
    }
}

void receive_send_timestamp(int sock)
{
    recvpacket(sock, MSG_ERRQUEUE);
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
        cout << "Data is available now.\n";
    }
    else
    {
        cout << "No data within five seconds.\n";
    }
}
