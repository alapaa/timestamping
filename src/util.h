#ifndef _UTIL_H_
#define _UTIL_H_

#include <string>
#include <netinet/in.h>

using std::string;

void do_bind(int sock, sockaddr_storage *ss);
void set_nonblocking(int sock);
int setup_socket(int domain, int type, int so_timestamping_flags);
void setup_device(int sock, string iface_name, int so_timestamping_flags);
void receive_send_timestamp(int sock);
void create_sockaddr_storage(int domain, string address, in_port_t port, sockaddr_storage *ssp);
void wait_for_errqueue_data(int sock);
void recvpacket(int sock, int recvmsg_flags, char **databuf, int *data_len, sockaddr_storage **ss);
void sendpacket(int domain, string address, in_port_t port, int sock);
void sendpacket(sockaddr_storage *ss, int sock, char *buf, size_t buflen);
#endif
