#include <cppunit/TestAssert.h>
#include "end_to_end_tests_base.h"
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
#include <libcmm_net_restriction.h>
#include <openssl/sha.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <functional>
using std::min;

#include "test_common.h"


const short EndToEndTestsBase::TEST_PORT = 4242;
short EndToEndTestsBase::listen_port = TEST_PORT;

bool EndToEndTestsBase::static_inited = false;
int EndToEndTestsBase::listen_sock = -1;
char *EndToEndTestsBase::hostname = NULL;

void
EndToEndTestsBase::setListenPort(short port)
{
    listen_port = port;
}

void
EndToEndTestsBase::setupReceiver()
{
    listen_sock = socket(PF_INET, SOCK_STREAM, 0);
    handle_error(listen_sock < 0, "socket");
    
    int on = 1;
    int rc = setsockopt (listen_sock, SOL_SOCKET, SO_REUSEADDR,
                         (char *) &on, sizeof(on));
    if (rc < 0) {
        DEBUG_LOG("Cannot reuse socket address\n");
    }
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(listen_port);
    
    socklen_t addrlen = sizeof(addr);
    rc = bind(listen_sock, (struct sockaddr*)&addr, addrlen);
    handle_error(rc < 0, "bind");
    
    rc = cmm_listen(listen_sock, 5);
    handle_error(rc < 0, "cmm_listen");
    DEBUG_LOG("Receiver is listening...\n");
}

void 
EndToEndTestsBase::setUp()
{
    //int rc = -1;
    // rc = system("ps aux | grep -v grep | grep conn_scout > /dev/null");
    // if (rc != 0) {
    //     DEBUG_LOG("conn_scout is not running; please start it first.\n");
    //     exit(EXIT_FAILURE);
    // }

    if (!static_inited) {
        chooseRole();
        if (isReceiver()) {
            setupReceiver();
        }
        
        static_inited = true;
    }
    
    if (isReceiver()) {
        socketSetup();

        startReceiver();
    } else {
        waitForReceiver();

        DEBUG_LOG("Starting sender\n");
        startSender();
    }
}

void 
EndToEndTestsBase::tearDown()
{
    if (isReceiver()) {
        cmm_shutdown(data_sock, SHUT_RDWR);
        cmm_close(data_sock);
    } else {
        cmm_shutdown(data_sock, SHUT_RDWR);
        cmm_close(data_sock);
    }
}

void
EndToEndTestsBase::startReceiver()
{
    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);

    data_sock = cmm_accept(listen_sock, 
                           (struct sockaddr *)&addr,
                           &addrlen);
    handle_error(data_sock < 0, "cmm_accept");
    DEBUG_LOG("Receiver accepted connection %d\n", data_sock);
}

void
EndToEndTestsBase::setRemoteHost(const char *hostname_)
{
    assert(hostname == NULL);
    hostname = strdup(hostname_);
}

extern int g_network_strategy;

void
EndToEndTestsBase::startSender()
{
    data_sock = cmm_socket(PF_INET, SOCK_STREAM, 0);
    handle_error(data_sock < 0, "cmm_socket");

    socketSetup();

    int rc = cmm_setsockopt(data_sock, SOL_SOCKET,
                            SO_CMM_REDUNDANCY_STRATEGY,
                            &g_network_strategy, sizeof(g_network_strategy));
    handle_error(rc < 0, "cmm_setsockopt");

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;

    if (!strcmp(hostname, "localhost")) {
        DEBUG_LOG("Using INADDR_LOOPBACK\n");
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    } else {
        DEBUG_LOG("Looking up %s\n", hostname);
        struct hostent *he = gethostbyname(hostname);
        if (!he) {
            herror("gethostbyname");
            //exit(EXIT_FAILURE);
            abort_test("gethostbyname failed");
        }
        
        memcpy(&addr.sin_addr, he->h_addr, he->h_length);
        DEBUG_LOG("Resolved %s to %s\n", 
                hostname, inet_ntoa(addr.sin_addr));
    }
    addr.sin_port = htons(listen_port);

    socklen_t addrlen = sizeof(addr);

    DEBUG_LOG("Sender is connecting...\n");
    rc = cmm_connect(data_sock, (struct sockaddr*)&addr, addrlen);
    handle_error(rc < 0, "cmm_connect");

    DEBUG_LOG("Sender is connected.\n");
}

void
EndToEndTestsBase::receiveAndChecksum()
{
    int bytes = -1;
    int rc = cmm_recv(data_sock, &bytes, sizeof(bytes), 
                      MSG_WAITALL, NULL);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("Receiving message size", 
                                 (int)sizeof(bytes), rc);
    
    bytes = ntohl(bytes);
    DEBUG_LOG("Receiving %d random bytes and digest\n", bytes);
    
    unsigned char *buf = new unsigned char[bytes];
    rc = cmm_recv(data_sock, buf, bytes, 
                  MSG_WAITALL, NULL);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("Receiving message", bytes, rc);
    
    unsigned char digest[SHA_DIGEST_LENGTH];
    rc = cmm_recv(data_sock, digest, SHA_DIGEST_LENGTH, 
                  MSG_WAITALL, NULL);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("Receiving digest", 
                                 SHA_DIGEST_LENGTH, rc);
    
    unsigned char my_digest[SHA_DIGEST_LENGTH];
    unsigned char *result = SHA1(buf, bytes, my_digest);
    delete [] buf;
    CPPUNIT_ASSERT_EQUAL_MESSAGE("Receiver: computing digest", 
                                 (unsigned char*)my_digest, result);
    
    rc = memcmp(digest, my_digest, SHA_DIGEST_LENGTH);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("Receiver: checking that digests match", 
                                 0, rc);
    
    DEBUG_LOG("Received %d bytes, digest matches.\n", bytes);
    char c = 0;
    rc = cmm_write(data_sock, &c, 1, 0, NULL, NULL);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("Sent 1 byte", 1, rc);

    DEBUG_LOG("Receiver finished.\n");
}

void
EndToEndTestsBase::sendChecksum(unsigned char *buf, size_t bytes)
{
    unsigned char my_digest[SHA_DIGEST_LENGTH];
    unsigned char *result = SHA1(buf, bytes, my_digest);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("Sender: computing digest", 
                                 (unsigned char*)my_digest, result);
    
    int rc = cmm_send(data_sock, my_digest, SHA_DIGEST_LENGTH, 0, 
                      0, NULL, NULL);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("Sending digest", SHA_DIGEST_LENGTH, rc);
    char c = 0;
    rc = cmm_read(data_sock, &c, 1, NULL);
    CPPUNIT_ASSERT_MESSAGE("Received 1 byte", rc == 1 || rc == 0);
}

void
EndToEndTestsBase::sendMessageSize(int bytes)
{
    int nbytes = htonl(bytes);
    int rc = cmm_send(data_sock, &nbytes, sizeof(nbytes), 0, 0, NULL, NULL);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("Sending message size", 
                                 (int)sizeof(nbytes), rc);
}

void 
EndToEndTestsBase::testRandomBytesReceivedCorrectly()
{
    if (isReceiver()) {
        receiveAndChecksum();
    } else {
        const int bytes = 4096;
        unsigned char buf[bytes];
        int rand_fd = open("/dev/urandom", O_RDONLY);
        CPPUNIT_ASSERT_MESSAGE("Opening /dev/urandom", rand_fd != -1);
        
        //DEBUG_LOG("Reading %d bytes from /dev/urandom\n", bytes);
        int rc = read(rand_fd, buf, bytes);
        if (rc != bytes) {
            DEBUG_LOG("Failed to read random bytes! rc=%d, errno=%d\n",
                    rc, errno);
        }
        CPPUNIT_ASSERT_EQUAL_MESSAGE("Reading bytes from /dev/urandom",
                                     bytes, rc);
        close(rand_fd);
        
        DEBUG_LOG("Sending %d random bytes and digest\n", bytes);
        sendMessageSize(bytes);
        rc = cmm_send(data_sock, buf, bytes, 0, 0, NULL, NULL);
        CPPUNIT_ASSERT_EQUAL_MESSAGE("Sending message", bytes, rc);
        
        sendChecksum(buf, bytes);

        DEBUG_LOG("Sender finished.\n");
    }
}

void
EndToEndTestsBase::testNoInterleaving()
{
    if (isReceiver()) {
        receiveAndChecksum();
    } else {
        const int numints = 1024;
        unsigned int ints[numints];
        irob_id_t irob_ids[numints];
        for (int i = 0; i < numints; ++i) {
            ints[i] = 0xdeadbeef;
            irob_ids[i] = -1;
        }

        DEBUG_LOG("Sending %d integers in byte-sized chunks, "
               "in separate IROBs\n", numints);
        sendMessageSize(sizeof(int)*numints);
        for (int i = 0; i < (int)sizeof(int); ++i) {
            for (int j = 0; j < numints; ++j) {
                if (irob_ids[j] == -1) {
                    irob_ids[j] = begin_irob(data_sock, 0, NULL, 
                                             0, NULL, NULL);
                    CPPUNIT_ASSERT_MESSAGE("begin_irob succeeds", 
                                           irob_ids[j] >= 0);
                }
                char *one_int = (char*)&ints[j];
                int rc = irob_send(irob_ids[j], (void*)&one_int[i], 1, 0);
                CPPUNIT_ASSERT_EQUAL_MESSAGE("Sending one byte succeeds", 
                                             1, rc);
                if (i + 1 == (int)sizeof(int)) {
                    rc = end_irob(irob_ids[j]);
                }
            }
        }
        sendChecksum((unsigned char*)ints, sizeof(int)*numints);
        DEBUG_LOG("Sender finished.\n");
    }
}

void
EndToEndTestsBase::testPartialRecv()
{
    /* Property: if recv is called for N bytes and M bytes are
     *           available, and M < N, recv will not block for the
     *           full N bytes but will intstead return the M available
     *           bytes.  cmm_recv should do likewise.
     */

    const char msg[] = "Hi, I'm Woody! Howdy howdy howdy.";
    int msglen = sizeof(msg);
    int send_chunksize = 3;
    if (isReceiver()) {
        const int blocksize = msglen;
        char buf[blocksize];
        memset(buf, 0, blocksize);

        DEBUG_LOG("Attempting to receive %d bytes "
                "with a stuttering sender\n", msglen);

        int bytes_recvd = 0;
        while (bytes_recvd < msglen) {
            int rc = cmm_read(data_sock, buf, blocksize, NULL);
            //handle_error(rc <= 0, "cmm_read");
            int expected_chunksize = min(send_chunksize, msglen - bytes_recvd);
            CPPUNIT_ASSERT_EQUAL_MESSAGE("Recv returns data as it arrives",
                                         expected_chunksize, rc);

            int cmp = memcmp(msg + bytes_recvd, buf, rc);
            CPPUNIT_ASSERT_EQUAL_MESSAGE("Block content matches msg", 0, cmp);
            
            bytes_recvd += rc;
            int resp = htonl(rc);
            rc = cmm_write(data_sock, &resp, sizeof(resp), 0, NULL, NULL);
            CPPUNIT_ASSERT_EQUAL_MESSAGE("Sending response integer",
                                         (int)sizeof(resp), rc);
        }
        DEBUG_LOG("Received %d bytes successfully.\n",
                bytes_recvd);
        CPPUNIT_ASSERT_EQUAL_MESSAGE("Received all bytes",
                                     msglen, bytes_recvd);        
        char c = 0;
        int rc = cmm_write(data_sock, &c, 1, 0, NULL, NULL);
        CPPUNIT_ASSERT_EQUAL_MESSAGE("Sending one byte succeeds",
                                     1, rc);

    } else {
        int bytes_sent = 0;
        while (bytes_sent < msglen) {
            send_chunksize = min(send_chunksize, msglen - bytes_sent);
            int rc = cmm_write(data_sock, msg + bytes_sent,
                               send_chunksize, 0, NULL, NULL);
            CPPUNIT_ASSERT_EQUAL_MESSAGE("cmm_send succeeds",
                                         send_chunksize, rc);
            bytes_sent += rc;

            int resp = 0;
            rc = cmm_read(data_sock, (char*)&resp, sizeof(resp), NULL);
            CPPUNIT_ASSERT_EQUAL_MESSAGE("Reading response",
                                         (int)sizeof(resp), rc);
            resp = ntohl(resp);
            CPPUNIT_ASSERT_EQUAL_MESSAGE("Response is number of bytes recvd",
                                         send_chunksize, resp);
        }
        CPPUNIT_ASSERT_EQUAL_MESSAGE("Sent all bytes",
                                     msglen, bytes_sent);

        char c = 0;
        int rc = cmm_read(data_sock, &c, 1, NULL);
        CPPUNIT_ASSERT_EQUAL_MESSAGE("Reading one byte succeeds",
                                     1, rc);
    }
}

void
EndToEndTestsBase::testCMMPoll()
{
    const char ANSWER = 42;
    if (isReceiver()) {
        struct pollfd fd;
        fd.fd = data_sock;
        fd.events = POLLIN;
        fd.revents = 0;

        DEBUG_LOG("Receiver: polling for 3 seconds\n");
        int rc = cmm_poll(&fd, 1, 3000);
        CPPUNIT_ASSERT_EQUAL_MESSAGE("First poll times out",
                                     0, rc);
        DEBUG_LOG("Receiver: polling for 3 seconds\n");
        rc = cmm_poll(&fd, 1, -1);
        CPPUNIT_ASSERT_EQUAL_MESSAGE("Second poll returns 1",
                                     1, rc);
        CPPUNIT_ASSERT_MESSAGE("Data ready for reading",
                               fd.revents & POLLIN);
        char c = 0;
        rc = cmm_read(data_sock, &c, 1, NULL);
        CPPUNIT_ASSERT_EQUAL_MESSAGE("Reading one byte succeeds",
                                     1, rc);
        CPPUNIT_ASSERT_EQUAL_MESSAGE("Received byte is correct",
                                     ANSWER, c);
    } else {
        DEBUG_LOG("Sender: waiting 5 seconds\n");
        sleep(5);
        DEBUG_LOG("Sender: sending one byte\n");
        char c = ANSWER;
        int rc = cmm_send(data_sock, &c, 1, 0, 0, NULL, NULL);
        CPPUNIT_ASSERT_EQUAL_MESSAGE("Sending one byte succeeds",
                                     1, rc);
        sleep(3);
    }
}


static bool is_sorted(const int nums[], size_t n)
{
    for (size_t i = 0; i < n - 1; ++i) {
        if (nums[i] > nums[i+1]) {
            DEBUG_LOG("Not sorted! [ ");
            for (size_t j = 0; j < n; ++j) {
                DEBUG_LOG("%d ", nums[j]);
            }
            DEBUG_LOG("]\n");
            return false;
        }
    }
    return true;
}

void
EndToEndTestsBase::receiverAssertIntsSorted(int nums[], size_t n)
{
    int rc = cmm_recv(data_sock, nums, n*sizeof(int), 
                      MSG_WAITALL, NULL);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("Receiving integers",
                                 (int)(n*sizeof(int)), rc);
    
    DEBUG_LOG("Received 10 integers, checking sorting\n");
    for (size_t i = 0; i < n; ++i) {
        nums[i] = ntohl(nums[i]);
    }
    CPPUNIT_ASSERT_MESSAGE("Integers are sorted least-to-greatest",
                           is_sorted(nums, n));

    DEBUG_LOG("Received integers in correct order.\n");
}

void
EndToEndTestsBase::testHalfShutdown()
{
    DEBUG_LOG("Testing half-shutdown socket\n");
    if (isReceiver()) {
        for (int i = 0; i < 2; ++i) {
            int num = -1;
            int rc = cmm_read(data_sock, &num, sizeof(num), NULL);
            CPPUNIT_ASSERT_EQUAL_MESSAGE("Receiving integer",
                                         (int)sizeof(int), rc);
            CPPUNIT_ASSERT_EQUAL_MESSAGE("Integer is correct",
                                         i, (int)ntohl(num));
            DEBUG_LOG("Receiver: received %d\n", (int)ntohl(num));

            sleep(1);

            DEBUG_LOG("Receiver: sending %d\n", i);
            rc = cmm_write(data_sock, &num, sizeof(num), 
                               0, NULL, NULL);
            CPPUNIT_ASSERT_EQUAL_MESSAGE("Sending integer",
                                         (int)sizeof(int), rc);
        }
        DEBUG_LOG("Receiver done.\n");
    } else {
        for (int i = 0; i < 2; ++i) {
            DEBUG_LOG("Sender: sending %d\n", i);
            int num = htonl(i);
            int rc = cmm_write(data_sock, &num, sizeof(num), 
                               0, NULL, NULL);
            CPPUNIT_ASSERT_EQUAL_MESSAGE("Sending integer",
                                         (int)sizeof(int), rc);            
            
            if (i == 1) {
                DEBUG_LOG("Sender: shutting down with SHUT_WR\n");
                rc = cmm_shutdown(data_sock, SHUT_WR);
                CPPUNIT_ASSERT_EQUAL_MESSAGE("Shutdown succedds",
                                             0, rc);
            }

            rc = cmm_read(data_sock, &num, sizeof(num), NULL);
            CPPUNIT_ASSERT_EQUAL_MESSAGE("Receiving integer",
                                         (int)sizeof(int), rc);
            CPPUNIT_ASSERT_EQUAL_MESSAGE("Integer is correct",
                                         i, (int)ntohl(num));
            DEBUG_LOG("Sender: received %d\n", (int)ntohl(num));
        }
        DEBUG_LOG("Sender done.\n");
    }
}
