#ifndef _UTIL_H_
#define _UTIL_H_

#include <string>
#include <netinet/in.h>

using std::string;

int setup_socket(int domain, int type, int so_timestamping_flags);
void setup_device(int sock, string iface_name, int so_timestamping_flags);
void receive_send_timestamp(int sock);
void create_sockaddr_storage(int domain, string address, in_port_t port, sockaddr_storage *ssp);
void wait_for_errqueue_data(int sock);

#endif
