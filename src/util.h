#ifndef _UTIL_H_
#define _UTIL_H_

#include <tuple>
#include <memory>
#include <string>

#include <netinet/in.h>

#define FILELINE string(__FILE__) + string(":") + string(to_string(__LINE__))
// #define FILELINE string()

std::string exec(const char* cmd);
void check_equal_addresses(sockaddr_storage *ss1, sockaddr_storage *ss2);
void do_bind(int sock, sockaddr_storage *ss);
void set_nonblocking(int sock);
int setup_socket(int domain, int type, int so_timestamping_flags);
void setup_device(int sock, std::string iface_name, int so_timestamping_flags);
std::tuple<std::shared_ptr<char>, int, sockaddr_storage, timespec> receive_send_timestamp(int sock, bool use_sw_tstamp);
void create_sockaddr_storage(int domain, std::string address, in_port_t port, sockaddr_storage *ssp);
void wait_for_errqueue_data(int sock);
std::tuple<std::shared_ptr<char>, int, sockaddr_storage, timespec> recvpacket(int sock, int recvmsg_flags, bool use_sw_tstamp);
void sendpacket(int domain, std::string address, in_port_t port, int sock, char *buf, size_t buflen);
void sendpacket(sockaddr_storage *ss, int sock, char *buf, size_t buflen);

#endif
