#ifndef _GPL_CODE_REMOVE_H_
#include <sys/socket.h>

void printpacket(struct msghdr *msg, int res,
                 int sock, int recvmsg_flags,
                 int siocgstamp, int siocgstampns, timespec *ts_result);
#endif
