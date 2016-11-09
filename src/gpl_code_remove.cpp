#include <iostream>
#include <cstdio>
#include <cstring>

#include <sys/time.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>

#include <linux/errqueue.h>

#include "gpl_code_remove.h"

// Disable printf:
#define printf(fmt, ...) (0)

using std::cout;


// TODO: Remove/remake printpacket, it is GPL and uses C-style printf.
void printpacket(struct msghdr *msg, int res,
                 int sock, int recvmsg_flags,
                 int siocgstamp, int siocgstampns, timespec *ts_result)
{
    struct sockaddr_in *from_addr = (struct sockaddr_in *)msg->msg_name;
    struct cmsghdr *cmsg;
    struct timeval tv;
    struct timespec ts;
    struct timeval now;
    memset(ts_result, 0, sizeof(*ts_result));

    gettimeofday(&now, 0);

    printf("%ld.%06ld: received %s data, %d bytes from %s, %zu bytes control messages\n",
           (long)now.tv_sec, (long)now.tv_usec,
           (recvmsg_flags & MSG_ERRQUEUE) ? "error" : "regular",
           res,
           inet_ntoa(from_addr->sin_addr),
           msg->msg_controllen);
    for (cmsg = CMSG_FIRSTHDR(msg);
         cmsg;
         cmsg = CMSG_NXTHDR(msg, cmsg)) {
        printf("   cmsg len %zu: ", cmsg->cmsg_len);
        switch (cmsg->cmsg_level) {
        case SOL_SOCKET:
            printf("SOL_SOCKET ");
            switch (cmsg->cmsg_type) {
            case SO_TIMESTAMP: {
                struct timeval *stamp =
                    (struct timeval *)CMSG_DATA(cmsg);
                printf("SO_TIMESTAMP %ld.%06ld",
                       (long)stamp->tv_sec,
                       (long)stamp->tv_usec);
                break;
            }
            case SO_TIMESTAMPNS: {
                struct timespec *stamp =
                    (struct timespec *)CMSG_DATA(cmsg);
                printf("SO_TIMESTAMPNS %ld.%09ld",
                       (long)stamp->tv_sec,
                       (long)stamp->tv_nsec);
                break;
            }
            case SO_TIMESTAMPING: {
                struct timespec *stamp =
                    (struct timespec *)CMSG_DATA(cmsg);
                printf("SO_TIMESTAMPING ");
                printf("SW %ld.%09ld ",
                       (long)stamp->tv_sec,
                       (long)stamp->tv_nsec);
                stamp++;
                /* skip deprecated HW transformed */
                stamp++;
                printf("HW raw %ld.%09ld",
                       (long)stamp->tv_sec,
                       (long)stamp->tv_nsec);
                *ts_result = *stamp;
                break;
            }
            default:
                printf("type %d", cmsg->cmsg_type);
                break;
            }
            break;
        case IPPROTO_IP:
            printf("IPPROTO_IP ");
            switch (cmsg->cmsg_type) {
            case IP_RECVERR: {
                struct sock_extended_err *err =
                    (struct sock_extended_err *)CMSG_DATA(cmsg);
                printf("IP_RECVERR ee_errno '%s' ee_origin %d => %s",
                    strerror(err->ee_errno),
                    err->ee_origin,
#ifdef SO_EE_ORIGIN_TIMESTAMPING
                    err->ee_origin == SO_EE_ORIGIN_TIMESTAMPING ?
                    "bounced packet" : "unexpected origin"
#else
                    "probably SO_EE_ORIGIN_TIMESTAMPING"
#endif
                    );
                // if (res < sizeof(sync))
                //     printf(" => truncated data?!");
                // else if (!memcmp(sync, data + res - sizeof(sync),
                //             sizeof(sync)))
                //     printf(" => GOT OUR DATA BACK (HURRAY!)");
                break;
            }
            case IP_PKTINFO: {
                in_addr tmpaddr;
                struct in_pktinfo *pktinfo =
                    (struct in_pktinfo *)CMSG_DATA(cmsg);
                printf("IP_PKTINFO interface index %u, ",
                    pktinfo->ipi_ifindex);
                //tmpaddr = pktinfo->ipi_addr;
                //cout << "message received on address " << inet_ntoa(tmpaddr) << '\n';
                break;
            }
            default:
                printf("type %d", cmsg->cmsg_type);
                break;
            }
            break;
        default:
            printf("level %d type %d",
                cmsg->cmsg_level,
                cmsg->cmsg_type);
            break;
        }
        printf("\n");
    }

    if (siocgstamp) {
        if (ioctl(sock, SIOCGSTAMP, &tv))
            printf("   %s: %s\n", "SIOCGSTAMP", strerror(errno));
        else
            printf("SIOCGSTAMP %ld.%06ld\n",
                   (long)tv.tv_sec,
                   (long)tv.tv_usec);
    }
    if (siocgstampns) {
        if (ioctl(sock, SIOCGSTAMPNS, &ts))
            printf("   %s: %s\n", "SIOCGSTAMPNS", strerror(errno));
        else
            printf("SIOCGSTAMPNS %ld.%09ld\n",
                   (long)ts.tv_sec,
                   (long)ts.tv_nsec);
    }
}
