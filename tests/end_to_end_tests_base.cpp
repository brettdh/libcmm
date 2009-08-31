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
#include <openssl/sha.h>
#include <sys/stat.h>
#include <fcntl.h>

static const short TEST_PORT = 4242;

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
        fprintf(stderr, "Cannot reuse socket address\n");
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
    printf("Receiver is listening...\n");
}

void 
EndToEndTestsBase::setUp()
{
    int rc = -1;
    rc = system("ps aux | grep -v grep | grep conn_scout > /dev/null");
    if (rc != 0) {
        fprintf(stderr, "conn_scout is not running; please start it first.\n");
        exit(EXIT_FAILURE);
    }

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

        printf("Starting sender\n");
        startSender();
    }
}

void 
EndToEndTestsBase::tearDown()
{
    if (isReceiver()) {
        cmm_close(read_sock);
    } else {
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
    printf("Receiver accepted connection %d\n", read_sock);
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
    
    printf("Looking up %s\n", hostname);
    struct hostent *he = gethostbyname(hostname);
    if (!he) {
        herror("gethostbyname");
        exit(EXIT_FAILURE);
    }

    memcpy(&addr.sin_addr, he->h_addr, he->h_length);
    printf("Resolved %s to %s\n", hostname, inet_ntoa(addr.sin_addr));
    addr.sin_port = htons(TEST_PORT);

    socklen_t addrlen = sizeof(addr);

    printf("Sender is connecting...\n");
    int rc = cmm_connect(send_sock, (struct sockaddr*)&addr, addrlen);
    handle_error(rc < 0, "cmm_connect");

    printf("Sender is connected.\n");
}

void 
EndToEndTestsBase::testRandomBytesReceivedCorrectly()
{
    if (isReceiver()) {
        int bytes = -1;
        int rc = cmm_read(read_sock, &bytes, sizeof(bytes), NULL);
        CPPUNIT_ASSERT_EQUAL_MESSAGE("Receiving message size", 
                                     (int)sizeof(bytes), rc);
        
        bytes = ntohl(bytes);
        printf("Receiving %d random bytes and digest\n", bytes);
        
        unsigned char *buf = new unsigned char[bytes];
        rc = cmm_read(read_sock, buf, bytes, NULL);
        CPPUNIT_ASSERT_EQUAL_MESSAGE("Receiving message", bytes, rc);
        
        unsigned char digest[SHA_DIGEST_LENGTH];
        rc = cmm_read(read_sock, digest, SHA_DIGEST_LENGTH, NULL);
        CPPUNIT_ASSERT_EQUAL_MESSAGE("Receiving digest", 
                                     SHA_DIGEST_LENGTH, rc);
        
        unsigned char my_digest[SHA_DIGEST_LENGTH];
        unsigned char *result = SHA1(buf, bytes, my_digest);
        CPPUNIT_ASSERT_EQUAL_MESSAGE("Receiver: computing digest", 
                                     (unsigned char*)my_digest, result);
        
        rc = memcmp(digest, my_digest, SHA_DIGEST_LENGTH);
        CPPUNIT_ASSERT_EQUAL_MESSAGE("Receiver: checking that digests match", 
                                     0, rc);
        
        printf("Received %d bytes, digest matches.\n", bytes);
        printf("Receiver finished.\n");
    } else {
        int bytes = 4096;
        unsigned char *buf = new unsigned char[bytes];
        int rand_fd = open("/dev/urandom", O_RDONLY);
        CPPUNIT_ASSERT_MESSAGE("Opening /dev/urandom", rand_fd != -1);
        
        //printf("Reading %d bytes from /dev/urandom\n", bytes);
        int rc = read(rand_fd, buf, bytes);
        if (rc != bytes) {
            fprintf(stderr, "Failed to read random bytes! rc=%d, errno=%d\n",
                    rc, errno);
        }
        CPPUNIT_ASSERT_EQUAL_MESSAGE("Reading bytes from /dev/urandom",
                                     bytes, rc);
        close(rand_fd);
        
        printf("Sending %d random bytes and digest\n", bytes);
        int nbytes = htonl(bytes);
        rc = cmm_send(send_sock, &nbytes, sizeof(nbytes), 0, 0, NULL, NULL);
        CPPUNIT_ASSERT_EQUAL_MESSAGE("Sending message size", 
                                     (int)sizeof(nbytes), rc);
        
        rc = cmm_send(send_sock, buf, bytes, 0, 0, NULL, NULL);
        CPPUNIT_ASSERT_EQUAL_MESSAGE("Sending message", bytes, rc);
        
        unsigned char my_digest[SHA_DIGEST_LENGTH];
        unsigned char *result = SHA1(buf, bytes, my_digest);
        CPPUNIT_ASSERT_EQUAL_MESSAGE("Sender: computing digest", 
                                     (unsigned char*)my_digest, result);
        
        rc = cmm_send(send_sock, my_digest, SHA_DIGEST_LENGTH, 0, 
                      0, NULL, NULL);
        CPPUNIT_ASSERT_EQUAL_MESSAGE("Sending digest", SHA_DIGEST_LENGTH, rc);

        printf("Sender finished.\n");
    }
}

void handle_error(bool condition, const char *msg)
{
    if (condition) {
        perror(msg);
        exit(EXIT_FAILURE);
    }
}

static bool is_sorted(const int nums[], size_t n)
{
    for (size_t i = 0; i < n - 1; ++i) {
        if (nums[i] > nums[i+1]) {
            fprintf(stderr, "Not sorted! [ ");
            for (size_t j = 0; j < n; ++j) {
                fprintf(stderr, "%d ", nums[j]);
            }
            fprintf(stderr, "]\n");
            return false;
        }
    }
    return true;
}

void
EndToEndTestsBase::receiverAssertIntsSorted(int nums[], size_t n)
{
    int rc = cmm_read(read_sock, nums, n*sizeof(int), NULL);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("Receiving integers",
                                 (int)(n*sizeof(int)), rc);
    
    printf("Received 10 integers, checking sorting\n");
    for (size_t i = 0; i < n; ++i) {
        nums[i] = ntohl(nums[i]);
    }
    CPPUNIT_ASSERT_MESSAGE("Integers are sorted least-to-greatest",
                           is_sorted(nums, n));

    printf("Received integers in correct order.\n");
}
