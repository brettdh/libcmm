#include <cppunit/Test.h>
#include <cppunit/TestAssert.h>
#include "socket_api_tests.h"
#include "test_common.h"

#include <sys/socket.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <string.h>

#include <libcmm.h>
#include <libcmm_irob.h>
#include <libcmm_private.h>

#include <string>
using std::string;

CPPUNIT_TEST_SUITE_REGISTRATION(SocketAPITest);

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

// This test is disabled until I find time to fix the bug it exposes;
//  it's not high-priority for now.
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

void
SocketAPITest::testLabelsReturnedOnIROBBoundaries()
{
    if (isReceiver()) {
        sleep(3);
        u_long labels = 0;
        int values[2] = {0, 0};
        int rc = cmm_read(data_sock, values, sizeof(values), &labels);
        CPPUNIT_ASSERT_EQUAL_MESSAGE("Read only the first IROB", (int) sizeof(values[0]), rc);
        CPPUNIT_ASSERT_EQUAL_MESSAGE("Got expected labels for first IROB", CMM_LABEL_SMALL, (int)labels);

        rc = cmm_read(data_sock, &values[1], sizeof(values[1]), &labels);
        CPPUNIT_ASSERT_EQUAL_MESSAGE("Read the second IROB", (int) sizeof(values[1]), rc);
        CPPUNIT_ASSERT_EQUAL_MESSAGE("Got expected labels for second IROB", CMM_LABEL_LARGE, (int)labels);

        for (int i= 0; i < 2; ++i) {
            CPPUNIT_ASSERT_EQUAL_MESSAGE("Got expected data", 42U, ntohl(values[i]));
        }
    } else {
        int value = htonl(42);
        int rc = cmm_write(data_sock, &value, sizeof(value), CMM_LABEL_SMALL, NULL, NULL);
        CPPUNIT_ASSERT_EQUAL_MESSAGE("Sent data", (int) sizeof(value), rc);

        rc = cmm_write(data_sock, &value, sizeof(value), CMM_LABEL_LARGE, NULL, NULL);
        CPPUNIT_ASSERT_EQUAL_MESSAGE("Sent data", (int) sizeof(value), rc);
    }
}


void
SocketAPITest::testDroppedIROBFailureCases()
{
    if (isReceiver()) {
        char ch;
        cmm_read(data_sock, &ch, 1, NULL);
    } else {
        irob_id_t irob = begin_irob(data_sock, 0, NULL, 0, NULL, NULL);
        CPPUNIT_ASSERT(irob >= 0);
        
        CMM_PRIVATE_drop_irob_and_dependents(irob);
        
        int rc = irob_send(irob, "12345", 5, 0);
        CPPUNIT_ASSERT_MESSAGE("Adding to a dropped IROB should fail", rc < 0);
        CPPUNIT_ASSERT_EQUAL_MESSAGE("irob_chunk returns 'undeliverable' return code",
                                     CMM_UNDELIVERABLE, rc);

        rc = begin_irob(data_sock, 1, &irob, 0, NULL, NULL);
        CPPUNIT_ASSERT_MESSAGE("Creating an IROB that depends on a known-undelivered IROB should fail",
                               rc < 0);
        CPPUNIT_ASSERT_EQUAL_MESSAGE("begin_irob returns 'undeliverable' return code",
                                     CMM_UNDELIVERABLE, rc);

        cmm_write_with_deps(data_sock, "A", 1, 0, NULL, 0, NULL, NULL, NULL);
    }
}

void
SocketAPITest::testDroppedIROBWithDefaultIROBs()
{
    if (isReceiver()) {
        char ch;
        cmm_read(data_sock, &ch, 1, NULL);
    } else {
        irob_id_t irob = begin_irob(data_sock, 0, NULL, 0, NULL, NULL);
        CPPUNIT_ASSERT(irob >= 0);
        
        CMM_PRIVATE_drop_irob_and_dependents(irob);

        int rc = cmm_write(data_sock, "A", 1, 0, NULL, NULL);
        CPPUNIT_ASSERT_MESSAGE("A new default IROB should not depend on dropped IROBs",
                               rc != CMM_UNDELIVERABLE);
        CPPUNIT_ASSERT_EQUAL(1, rc);
    }
}

void
SocketAPITest::testPeek()
{
    const char msg[] = "This is the message.";
    size_t len = sizeof(msg);
    
    if (isReceiver()) {
        char buf[50];
        
        sleep(1);
        int rc;

        for (int i = 0; i < 5; ++i) {
            rc = cmm_recv(data_sock, buf, 5, MSG_PEEK, NULL);
            CPPUNIT_ASSERT_EQUAL(5, rc);
            buf[rc]= '\0';
            CPPUNIT_ASSERT_EQUAL(string(msg, 5), string(buf));
        }

        rc = cmm_recv(data_sock, buf, len, MSG_WAITALL, NULL);
        CPPUNIT_ASSERT_EQUAL((int) len, rc);
        buf[rc]= '\0';
        CPPUNIT_ASSERT_EQUAL(string(msg), string(buf));
    } else {
        int rc = cmm_write(data_sock, msg, len, 0, NULL, NULL);
        CPPUNIT_ASSERT_EQUAL((int) len, rc);
    }
}

void
SocketAPITest::setReceiveTimeout(int seconds)
{
    struct timeval timeout = {seconds, 0};
    int rc = cmm_setsockopt(data_sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    CPPUNIT_ASSERT_EQUAL(0, rc);
}

void
SocketAPITest::testReceiveTimeout()
{
    const char str[] = "This is the string.";
    const int len = sizeof(str);
    const int FIRST_CHUNK = 5;
    
    if (isReceiver()) {
        setReceiveTimeout(1);

        char ch = 42;

        int rc = cmm_read(data_sock, &ch, 1, NULL);
        int e = errno;
        CPPUNIT_ASSERT_EQUAL(-1, rc);
        CPPUNIT_ASSERT(e == EAGAIN || e == EWOULDBLOCK);

        setReceiveTimeout(0); // disables timeout entirely

        rc = cmm_read(data_sock, &ch, 1, NULL);
        CPPUNIT_ASSERT_EQUAL(1, rc);

        setReceiveTimeout(1);
        
        char buf[64];
        memset(buf, 0, 64);
        rc = cmm_read(data_sock, buf, len, NULL);
        CPPUNIT_ASSERT_EQUAL(FIRST_CHUNK, rc);

        setReceiveTimeout(5);
        rc = cmm_read(data_sock, buf, len, NULL);
        CPPUNIT_ASSERT_EQUAL(len - FIRST_CHUNK, rc);
    } else {
        sleep(3);
        char ch = 42;
        int rc = cmm_write(data_sock, &ch, 1, 0, NULL, NULL);
        CPPUNIT_ASSERT_EQUAL(1, rc);
        
        rc = cmm_write(data_sock, str, FIRST_CHUNK, 0, NULL, NULL);
        CPPUNIT_ASSERT_EQUAL(FIRST_CHUNK, rc);
        sleep(3);
        
        rc = cmm_write(data_sock, str, len - FIRST_CHUNK, 0, NULL, NULL);
        CPPUNIT_ASSERT_EQUAL(len - FIRST_CHUNK, rc);
    }
}
