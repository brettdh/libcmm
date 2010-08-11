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
#include <openssl/sha.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <functional>
using std::min;

#include "test_common.h"


const short EndToEndTestsBase::TEST_PORT = 4242;

bool EndToEndTestsBase::static_inited = false;
int EndToEndTestsBase::listen_sock = -1;
char *EndToEndTestsBase::hostname = NULL;

void
EndToEndTestsBase::setupReceiver()
{
    listen_sock = socket(PF_INET, SOCK_STREAM, 0);
    handle_error(listen_sock < 0, "socket");
    
    int on = 1;
    int rc = setsockopt (listen_sock, SOL_SOCKET, SO_REUSEADDR,
                         (char *) &on, sizeof(on));
    if (rc < 0) {
        LOG("Cannot reuse socket address\n");
    }
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(TEST_PORT);
    
    socklen_t addrlen = sizeof(addr);
    rc = bind(listen_sock, (struct sockaddr*)&addr, addrlen);
    handle_error(rc < 0, "bind");
    
    rc = cmm_listen(listen_sock, 5);
    handle_error(rc < 0, "cmm_listen");
    LOG("Receiver is listening...\n");
}

void 
EndToEndTestsBase::setUp()
{
    //int rc = -1;
    // rc = system("ps aux | grep -v grep | grep conn_scout > /dev/null");
    // if (rc != 0) {
    //     LOG("conn_scout is not running; please start it first.\n");
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
        startReceiver();
    } else {
        waitForReceiver();

        LOG("Starting sender\n");
        startSender();
    }
}

void 
EndToEndTestsBase::tearDown()
{
    if (isReceiver()) {
        cmm_shutdown(read_sock, SHUT_RDWR);
        cmm_close(read_sock);
    } else {
        cmm_shutdown(send_sock, SHUT_RDWR);
        cmm_close(send_sock);
    }
}

void
EndToEndTestsBase::startReceiver()
{
    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);

    read_sock = cmm_accept(listen_sock, 
                           (struct sockaddr *)&addr,
                           &addrlen);
    handle_error(read_sock < 0, "cmm_accept");
    LOG("Receiver accepted connection %d\n", read_sock);
}

void
EndToEndTestsBase::setRemoteHost(const char *hostname_)
{
    assert(hostname == NULL);
    hostname = strdup(hostname_);
}

void
EndToEndTestsBase::startSender()
{
    send_sock = cmm_socket(PF_INET, SOCK_STREAM, 0);
    handle_error(send_sock < 0, "cmm_socket");

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;

    if (!strcmp(hostname, "localhost")) {
        LOG("Using INADDR_LOOPBACK\n");
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    } else {
        LOG("Looking up %s\n", hostname);
        struct hostent *he = gethostbyname(hostname);
        if (!he) {
            herror("gethostbyname");
            //exit(EXIT_FAILURE);
            abort_test("gethostbyname failed");
        }
        
        memcpy(&addr.sin_addr, he->h_addr, he->h_length);
        LOG("Resolved %s to %s\n", 
                hostname, inet_ntoa(addr.sin_addr));
    }
    addr.sin_port = htons(TEST_PORT);

    socklen_t addrlen = sizeof(addr);

    LOG("Sender is connecting...\n");
    int rc = cmm_connect(send_sock, (struct sockaddr*)&addr, addrlen);
    handle_error(rc < 0, "cmm_connect");

    LOG("Sender is connected.\n");
}

void
EndToEndTestsBase::receiveAndChecksum()
{
    int bytes = -1;
    int rc = cmm_recv(read_sock, &bytes, sizeof(bytes), 
                      MSG_WAITALL, NULL);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("Receiving message size", 
                                 (int)sizeof(bytes), rc);
    
    bytes = ntohl(bytes);
    LOG("Receiving %d random bytes and digest\n", bytes);
    
    unsigned char *buf = new unsigned char[bytes];
    rc = cmm_recv(read_sock, buf, bytes, 
                  MSG_WAITALL, NULL);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("Receiving message", bytes, rc);
    
    unsigned char digest[SHA_DIGEST_LENGTH];
    rc = cmm_recv(read_sock, digest, SHA_DIGEST_LENGTH, 
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
    
    LOG("Received %d bytes, digest matches.\n", bytes);
    char c = 0;
    rc = cmm_write(read_sock, &c, 1, 0, NULL, NULL);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("Sent 1 byte", 1, rc);

    LOG("Receiver finished.\n");
}

void
EndToEndTestsBase::sendChecksum(unsigned char *buf, size_t bytes)
{
    unsigned char my_digest[SHA_DIGEST_LENGTH];
    unsigned char *result = SHA1(buf, bytes, my_digest);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("Sender: computing digest", 
                                 (unsigned char*)my_digest, result);
    
    int rc = cmm_send(send_sock, my_digest, SHA_DIGEST_LENGTH, 0, 
                      0, NULL, NULL);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("Sending digest", SHA_DIGEST_LENGTH, rc);
    char c = 0;
    rc = cmm_read(send_sock, &c, 1, NULL);
    CPPUNIT_ASSERT_MESSAGE("Received 1 byte", rc == 1 || rc == 0);
}

void
EndToEndTestsBase::sendMessageSize(int bytes)
{
    int nbytes = htonl(bytes);
    int rc = cmm_send(send_sock, &nbytes, sizeof(nbytes), 0, 0, NULL, NULL);
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
        
        //LOG("Reading %d bytes from /dev/urandom\n", bytes);
        int rc = read(rand_fd, buf, bytes);
        if (rc != bytes) {
            LOG("Failed to read random bytes! rc=%d, errno=%d\n",
                    rc, errno);
        }
        CPPUNIT_ASSERT_EQUAL_MESSAGE("Reading bytes from /dev/urandom",
                                     bytes, rc);
        close(rand_fd);
        
        LOG("Sending %d random bytes and digest\n", bytes);
        sendMessageSize(bytes);
        rc = cmm_send(send_sock, buf, bytes, 0, 0, NULL, NULL);
        CPPUNIT_ASSERT_EQUAL_MESSAGE("Sending message", bytes, rc);
        
        sendChecksum(buf, bytes);

        LOG("Sender finished.\n");
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

        LOG("Sending %d integers in byte-sized chunks, "
               "in separate IROBs\n", numints);
        sendMessageSize(sizeof(int)*numints);
        for (int i = 0; i < (int)sizeof(int); ++i) {
            for (int j = 0; j < numints; ++j) {
                if (irob_ids[j] == -1) {
                    irob_ids[j] = begin_irob(send_sock, 0, NULL, 
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
        LOG("Sender finished.\n");
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

        LOG("Attempting to receive %d bytes "
                "with a stuttering sender\n", msglen);

        int bytes_recvd = 0;
        while (bytes_recvd < msglen) {
            int rc = cmm_read(read_sock, buf, blocksize, NULL);
            //handle_error(rc <= 0, "cmm_read");
            int expected_chunksize = min(send_chunksize, msglen - bytes_recvd);
            CPPUNIT_ASSERT_EQUAL_MESSAGE("Recv returns data as it arrives",
                                         expected_chunksize, rc);

            int cmp = memcmp(msg + bytes_recvd, buf, rc);
            CPPUNIT_ASSERT_EQUAL_MESSAGE("Block content matches msg", 0, cmp);
            
            bytes_recvd += rc;
            int resp = htonl(rc);
            rc = cmm_write(read_sock, &resp, sizeof(resp), 0, NULL, NULL);
            CPPUNIT_ASSERT_EQUAL_MESSAGE("Sending response integer",
                                         (int)sizeof(resp), rc);
        }
        LOG("Received %d bytes successfully.\n",
                bytes_recvd);
        CPPUNIT_ASSERT_EQUAL_MESSAGE("Received all bytes",
                                     msglen, bytes_recvd);        
        char c = 0;
        int rc = cmm_write(read_sock, &c, 1, 0, NULL, NULL);
        CPPUNIT_ASSERT_EQUAL_MESSAGE("Sending one byte succeeds",
                                     1, rc);

    } else {
        int bytes_sent = 0;
        while (bytes_sent < msglen) {
            send_chunksize = min(send_chunksize, msglen - bytes_sent);
            int rc = cmm_write(send_sock, msg + bytes_sent,
                               send_chunksize, 0, NULL, NULL);
            CPPUNIT_ASSERT_EQUAL_MESSAGE("cmm_send succeeds",
                                         send_chunksize, rc);
            bytes_sent += rc;

            int resp = 0;
            rc = cmm_read(send_sock, (char*)&resp, sizeof(resp), NULL);
            CPPUNIT_ASSERT_EQUAL_MESSAGE("Reading response",
                                         (int)sizeof(resp), rc);
            resp = ntohl(resp);
            CPPUNIT_ASSERT_EQUAL_MESSAGE("Response is number of bytes recvd",
                                         send_chunksize, resp);
        }
        CPPUNIT_ASSERT_EQUAL_MESSAGE("Sent all bytes",
                                     msglen, bytes_sent);

        char c = 0;
        int rc = cmm_read(send_sock, &c, 1, NULL);
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
        fd.fd = read_sock;
        fd.events = POLLIN;
        fd.revents = 0;

        LOG("Receiver: polling for 3 seconds\n");
        int rc = cmm_poll(&fd, 1, 3000);
        CPPUNIT_ASSERT_EQUAL_MESSAGE("First poll times out",
                                     0, rc);
        LOG("Receiver: polling for 3 seconds\n");
        rc = cmm_poll(&fd, 1, -1);
        CPPUNIT_ASSERT_EQUAL_MESSAGE("Second poll returns 1",
                                     1, rc);
        CPPUNIT_ASSERT_MESSAGE("Data ready for reading",
                               fd.revents & POLLIN);
        char c = 0;
        rc = cmm_read(read_sock, &c, 1, NULL);
        CPPUNIT_ASSERT_EQUAL_MESSAGE("Reading one byte succeeds",
                                     1, rc);
        CPPUNIT_ASSERT_EQUAL_MESSAGE("Received byte is correct",
                                     ANSWER, c);
    } else {
        LOG("Sender: waiting 5 seconds\n");
        sleep(5);
        LOG("Sender: sending one byte\n");
        char c = ANSWER;
        int rc = cmm_send(send_sock, &c, 1, 0, 0, NULL, NULL);
        CPPUNIT_ASSERT_EQUAL_MESSAGE("Sending one byte succeeds",
                                     1, rc);
        sleep(3);
    }
}


static bool is_sorted(const int nums[], size_t n)
{
    for (size_t i = 0; i < n - 1; ++i) {
        if (nums[i] > nums[i+1]) {
            LOG("Not sorted! [ ");
            for (size_t j = 0; j < n; ++j) {
                LOG("%d ", nums[j]);
            }
            LOG("]\n");
            return false;
        }
    }
    return true;
}

void
EndToEndTestsBase::receiverAssertIntsSorted(int nums[], size_t n)
{
    int rc = cmm_recv(read_sock, nums, n*sizeof(int), 
                      MSG_WAITALL, NULL);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("Receiving integers",
                                 (int)(n*sizeof(int)), rc);
    
    LOG("Received 10 integers, checking sorting\n");
    for (size_t i = 0; i < n; ++i) {
        nums[i] = ntohl(nums[i]);
    }
    CPPUNIT_ASSERT_MESSAGE("Integers are sorted least-to-greatest",
                           is_sorted(nums, n));

    LOG("Received integers in correct order.\n");
}

void
EndToEndTestsBase::testHalfShutdown()
{
    LOG("Testing half-shutdown socket\n");
    if (isReceiver()) {
        for (int i = 0; i < 2; ++i) {
            int num = -1;
            int rc = cmm_read(read_sock, &num, sizeof(num), NULL);
            CPPUNIT_ASSERT_EQUAL_MESSAGE("Receiving integer",
                                         (int)sizeof(int), rc);
            CPPUNIT_ASSERT_EQUAL_MESSAGE("Integer is correct",
                                         i, (int)ntohl(num));
            LOG("Receiver: received %d\n", (int)ntohl(num));

            sleep(1);

            LOG("Receiver: sending %d\n", i);
            rc = cmm_write(read_sock, &num, sizeof(num), 
                               0, NULL, NULL);
            CPPUNIT_ASSERT_EQUAL_MESSAGE("Sending integer",
                                         (int)sizeof(int), rc);
        }
        LOG("Receiver done.\n");
    } else {
        for (int i = 0; i < 2; ++i) {
            LOG("Sender: sending %d\n", i);
            int num = htonl(i);
            int rc = cmm_write(send_sock, &num, sizeof(num), 
                               0, NULL, NULL);
            CPPUNIT_ASSERT_EQUAL_MESSAGE("Sending integer",
                                         (int)sizeof(int), rc);            
            
            if (i == 1) {
                LOG("Sender: shutting down with SHUT_WR\n");
                rc = cmm_shutdown(send_sock, SHUT_WR);
                CPPUNIT_ASSERT_EQUAL_MESSAGE("Shutdown succedds",
                                             0, rc);
            }

            rc = cmm_read(send_sock, &num, sizeof(num), NULL);
            CPPUNIT_ASSERT_EQUAL_MESSAGE("Receiving integer",
                                         (int)sizeof(int), rc);
            CPPUNIT_ASSERT_EQUAL_MESSAGE("Integer is correct",
                                         i, (int)ntohl(num));
            LOG("Sender: received %d\n", (int)ntohl(num));
        }
        LOG("Sender done.\n");
    }
}
