#include <cppunit/TestAssert.h>
#include "non_blocking_tests.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <libcmm.h>
#include <libcmm_irob.h>
#include <openssl/sha.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <functional>
using std::min;
#include "test_common.h"

static void set_nonblocking(int sock)
{
    (void)fcntl(sock, F_SETFL, 
                fcntl(sock, F_GETFL) | O_NONBLOCK);
}

static void sock_wait(int sock, bool input)
{
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(sock, &fds);
    int rc = cmm_select(sock + 1, 
                        input ? &fds : NULL,
                        input ? NULL : &fds,
                        NULL, NULL);
    handle_error(rc != 1, "cmm_select");
}

void
NonBlockingTestsBase::setupReceiverNB(EndToEndTestsBase *base)
{
    base->EndToEndTestsBase::setupReceiver();
    set_nonblocking(base->listen_sock);
}

void
NonBlockingTestsBase::startReceiverNB(EndToEndTestsBase *base)
{
    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);

    sock_wait(base->listen_sock, true);

    base->read_sock = cmm_accept(base->listen_sock, 
                                 (struct sockaddr *)&addr,
                                 &addrlen);
    handle_error(base->read_sock < 0, "cmm_accept");
    fprintf(stderr, "Receiver accepted connection %d\n", base->read_sock);
    set_nonblocking(base->read_sock);
}

void
NonBlockingTestsBase::startSenderNB(EndToEndTestsBase *base)
{
    base->send_sock = cmm_socket(PF_INET, SOCK_STREAM, 0);
    handle_error(base->send_sock < 0, "cmm_socket");

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;

    fprintf(stderr, "Looking up %s\n", EndToEndTestsBase::hostname);
    struct hostent *he = gethostbyname(EndToEndTestsBase::hostname);
    if (!he) {
        herror("gethostbyname");
        exit(EXIT_FAILURE);
    }

    memcpy(&addr.sin_addr, he->h_addr, he->h_length);
    fprintf(stderr, "Resolved %s to %s\n", EndToEndTestsBase::hostname,
           inet_ntoa(addr.sin_addr));
    addr.sin_port = htons(EndToEndTestsBase::TEST_PORT);

    socklen_t addrlen = sizeof(addr);

    set_nonblocking(base->send_sock);

    fprintf(stderr, "Sender is connecting...\n");
    int rc = cmm_connect(base->send_sock, (struct sockaddr*)&addr, addrlen);
    handle_error(rc == -1 && errno != EINPROGRESS, 
                 "unexpected connect() failure");

    fprintf(stderr, "Sender connection is in progress.\n");
    sock_wait(base->send_sock, false);

    int err = 0;
    socklen_t len = sizeof(err);
    rc = cmm_getsockopt(base->send_sock, SOL_SOCKET, SO_ERROR, &err, &len);
    handle_error(rc < 0, "cmm_getsockopt");
    handle_error(err != 0, "socket error");

    fprintf(stderr, "Sender is connected.\n");
}

void
NonBlockingTestsBase::testTransferNB(EndToEndTestsBase *base)
{
    const int THE_ANSWER = 42;
    if (base->isReceiver()) {
        sleep(1);
        int data = 0;
        fprintf(stderr, "Receiver about to read %zd bytes\n", sizeof(data));
        int rc = cmm_read(base->read_sock, &data, sizeof(data), NULL);
        CPPUNIT_ASSERT_EQUAL_MESSAGE("First read returns data",
                                     (int)sizeof(data), rc);

        fprintf(stderr, "Receiver about to read %zd bytes\n", sizeof(data));
        rc = cmm_read(base->read_sock, &data, sizeof(data), NULL);
        int err = errno;
        CPPUNIT_ASSERT_EQUAL_MESSAGE("Second read returns error",
                                     -1, rc);
        CPPUNIT_ASSERT_EQUAL_MESSAGE("Second read yields EAGAIN",
                                     EAGAIN, err);
        
        fprintf(stderr, "Receiver about to poke sender\n");
        rc = cmm_write(base->read_sock, &data, sizeof(data),
                       0, NULL, NULL);
        handle_error(rc != 4, "cmm_write");

        fprintf(stderr, "Receiver about to select\n");
        sock_wait(base->read_sock, true);

        fprintf(stderr, "Receiver about to read %zd bytes\n", sizeof(data));
        rc = cmm_read(base->read_sock, &data, sizeof(data), NULL);
        CPPUNIT_ASSERT_EQUAL_MESSAGE("Post-select read returns data",
                                     (int)sizeof(data), rc);        
        fprintf(stderr, "Receiver done.\n");
    } else {
        int data = ntohl(THE_ANSWER);
        fprintf(stderr, "Sender about to send %zd bytes\n", sizeof(data));
        int rc = cmm_write(base->send_sock, &data, sizeof(data),
                           0, NULL, NULL);
        CPPUNIT_ASSERT_EQUAL_MESSAGE("First write succeeds",
                                     (int)sizeof(data), rc);

        fprintf(stderr, "Sender about to select\n");
        sock_wait(base->send_sock, true);

        rc = cmm_read(base->send_sock, &data, sizeof(data), NULL);
        handle_error(rc != sizeof(data), "cmm_read");
        fprintf(stderr, "Sender got receiver's poke\n");

        fprintf(stderr, "Sender about to send %zd bytes\n", sizeof(data));
        rc = cmm_write(base->send_sock, &data, sizeof(data),
                       0, NULL, NULL);
        CPPUNIT_ASSERT_EQUAL_MESSAGE("Second write succeeds",
                                     (int)sizeof(data), rc);
        fprintf(stderr, "Sender done.\n");
    }
}

void
NonBlockingTestsBase::testFragmentationNB(EndToEndTestsBase *base)
{
    const char msg[] = "Hello and welcome to Milliway's";
    const int msglen = sizeof(msg);
    if (base->isReceiver()) {
        char buf[msglen];
        memset(buf, 0, msglen);
        int bytes_recvd = 0;
        while (bytes_recvd < msglen) {
            int rc = cmm_read(base->read_sock, buf + bytes_recvd, 
                              msglen - bytes_recvd, NULL);
            if (rc > 0) {
                bytes_recvd += rc;
            } else {
                CPPUNIT_ASSERT_MESSAGE("failed recv returns -1", rc < 0);
                CPPUNIT_ASSERT_EQUAL_MESSAGE("failed recv is EAGAIN", 
                                             EAGAIN, (int)errno);
                sock_wait(base->read_sock, true);
            }
        }
        CPPUNIT_ASSERT_EQUAL_MESSAGE("Received all bytes",
                                     msglen, bytes_recvd);
        CPPUNIT_ASSERT_EQUAL_MESSAGE("Message is correct",
                                     0, strcmp(msg, buf));
    } else {
        sock_wait(base->send_sock, false);
        
        int bytes_sent = 0;
        while (bytes_sent < msglen) {
            sleep(1);
            int chunksize = 3;
            chunksize = min(chunksize, msglen - bytes_sent);
            int rc = cmm_write(base->send_sock, msg + bytes_sent,
                               chunksize, 0, NULL, NULL);
            CPPUNIT_ASSERT_EQUAL_MESSAGE("cmm_send succeeds",
                                         chunksize, rc);
            bytes_sent += rc;
        }
        CPPUNIT_ASSERT_EQUAL_MESSAGE("Sent all bytes",
                                     msglen, bytes_sent);
    }
}
