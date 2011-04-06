#include <cppunit/Test.h>
#include <cppunit/TestAssert.h>
#include "socket_api_tests.h"
#include "test_common.h"

#include <sys/socket.h>
#include <sys/fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <libcmm.h>

CPPUNIT_TEST_SUITE_REGISTRATION(SocketAPITest);

static const int TEST_PORT = 4242;

void
SocketAPITest::socketSetup()
{
    if (isReceiver()) {
        setOpt(listen_sock, SO_SNDBUF);
        setOpt(listen_sock, SO_RCVBUF);
    } else {
        setOpt(data_sock, SO_SNDBUF);
        setOpt(data_sock, SO_RCVBUF);
    }
}


static const int sockbuf_size = 21000;
static const int expected_sockbuf_size = sockbuf_size * 2;

void SocketAPITest::setOpt(int sock, int opt)
{
    char msg[64];
    snprintf(msg, sizeof(msg) - 1, "Set %s buffer sockopt", 
             opt == SO_SNDBUF ? "send" : "receive");
    
    int rc = cmm_setsockopt(sock, SOL_SOCKET, opt, 
                            &sockbuf_size, sizeof(sockbuf_size));
    CPPUNIT_ASSERT_EQUAL_MESSAGE(msg, 0, rc);
}

void SocketAPITest::checkOpt(int sock, int opt)
{
    char get_msg[64];
    char check_msg[64];
    snprintf(get_msg, sizeof(get_msg) - 1, "Get %s buffer sockopt", 
             opt == SO_SNDBUF ? "send" : "receive");
    snprintf(check_msg, sizeof(check_msg) - 1, "Check %s buffer sockopt", 
             opt == SO_SNDBUF ? "send" : "receive");
    
    int real_val = 0;
    socklen_t optlen = sizeof(real_val);
    int rc = cmm_getsockopt(sock, SOL_SOCKET, opt, 
                            &real_val, &optlen);
    CPPUNIT_ASSERT_EQUAL_MESSAGE(get_msg, 0, rc);
    CPPUNIT_ASSERT_EQUAL_MESSAGE(check_msg, expected_sockbuf_size, real_val);
}

void 
SocketAPITest::testBuffers()
{
    checkOpt(data_sock, SO_SNDBUF);
    checkOpt(data_sock, SO_RCVBUF);
    if (isReceiver()) {
        checkOpt(listen_sock, SO_SNDBUF);
        checkOpt(listen_sock, SO_RCVBUF);
    }
}
