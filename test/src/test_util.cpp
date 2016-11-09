#include <iostream>
#include <string>
#include <system_error>

#include <sys/socket.h>
#include <sys/un.h>
#include <bsd/string.h>

#include "gtest/gtest.h"

#include "util.h"

#ifndef UNIX_PATH_MAX
#define UNIX_PATH_MAX 108
#endif

using std::string;
using std::cout;

class UtilTest : public testing::Test
{
protected:
    virtual void SetUp() {

    }
};

using std::shared_ptr;

TEST_F(UtilTest, CreateSockaddr)
{
    sockaddr_storage ss;
    sockaddr_in *addrp;
    create_sockaddr_storage(AF_INET, "1.2.3.4", 5000, &ss);
    addrp = reinterpret_cast<sockaddr_in *>(&ss);
    EXPECT_EQ(addrp->sin_family, AF_INET);
}

TEST_F(UtilTest, RecvPacket)
{
    int retval;
    int sock;
    int sock2;
    char buf[] = "ABCDE12345";
    shared_ptr<char> data;
    int datalen = 0;
    sockaddr_storage ss;
    timespec hwts;

    sock = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (sock == -1)
    {
        throw std::system_error(errno, std::system_category());
    }

    sock2 = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (sock2 == -1)
    {
        throw std::system_error(errno, std::system_category());
    }

    sockaddr_un su;
    sockaddr_storage send_ss;
    memset(&send_ss, 0, sizeof(send_ss));
    su.sun_family = AF_UNIX;
    const char dummypath[] = "/tmp/dummy";
    strlcpy(su.sun_path, dummypath, UNIX_PATH_MAX);
    memcpy(&send_ss, &su, sizeof(su));

    retval = unlink(dummypath);
    if (retval == -1)
    {
        cout << "Could not unlink path \'" << dummypath << "\'\n";
    }
    retval = bind(sock2, (sockaddr *)&su, sizeof(su));
    if (retval == -1)
    {
        throw std::system_error(errno, std::system_category());
    }

    retval = sendto(sock, buf, sizeof(buf), 0, (sockaddr *)&su, sizeof(su));
    if (retval == -1)
    {
        throw std::system_error(errno, std::system_category());
    }

    tie(data, datalen, ss, hwts) = recvpacket(sock2, 0, false);
    //check_equal_addresses(&ss, &send_ss);
    string received_buf(data.get());
    EXPECT_EQ(received_buf, string(buf));
}

TEST_F(UtilTest, RecvPacketRealUdp)
{
    int sock;
    int sock2;
    const size_t BUFLEN = 1472;
    char buf[BUFLEN] = "ABCDE12345";
    shared_ptr<char> data;
    int datalen = 0;
    int domain = AF_INET;
    int listen_port = 5000;
    string addr("127.0.0.1");
    sockaddr_storage expected_sender_addr;
    sockaddr_storage ss;
    timespec hwts;

    sock = socket(domain, SOCK_DGRAM, 0);
    if (sock == -1)
    {
        throw std::system_error(errno, std::system_category());
    }

    sock2 = socket(domain, SOCK_DGRAM, 0);
    if (sock2 == -1)
    {
        throw std::system_error(errno, std::system_category());
    }

    create_sockaddr_storage(domain, addr, listen_port, &expected_sender_addr);
    do_bind(sock2, &expected_sender_addr);

    sendpacket(domain, addr, listen_port, sock, buf, sizeof(buf));
    tie(data, datalen, ss, hwts) = recvpacket(sock2, 0, false);

    check_equal_addresses(&ss, &expected_sender_addr);
    string received_buf(data.get());
    EXPECT_EQ(received_buf, string(buf));
}
