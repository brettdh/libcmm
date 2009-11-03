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


static void set_nonblocking(int sock)
{
    (void)fcntl(sock, F_SETFL, 
                fcntl(sock, F_GETFL) | O_NONBLOCK);
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

    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(base->listen_sock, &fds);
    int rc = select(base->listen_sock + 1, &fds, NULL, NULL, NULL);
    handle_error(rc < 0, "read-select");

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
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(base->send_sock, &fds);
    rc = cmm_select(base->send_sock + 1, NULL, &fds, NULL, NULL);
    handle_error(rc < 0, "write-select");

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

        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(base->read_sock, &fds);
        fprintf(stderr, "Receiver about to select\n");
        rc = cmm_select(base->read_sock + 1, &fds, NULL, NULL, NULL);
        handle_error(rc != 1, "cmm_select");

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

        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(base->send_sock, &fds);
        fprintf(stderr, "Sender about to select\n");
        rc = cmm_select(base->send_sock + 1, &fds, NULL, NULL, NULL);
        handle_error(rc != 1, "cmm_select");        

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
