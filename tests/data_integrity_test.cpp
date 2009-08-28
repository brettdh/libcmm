#include <cppunit/extensions/HelperMacros.h>
#include "data_integrity_test.h"
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <openssl/sha.h>
#include <assert.h>
#include <libcmm.h>
#include <libcmm_irob.h>

CPPUNIT_TEST_SUITE_REGISTRATION(DataIntegrityTest);

static const short TEST_PORT = 9876;

static bool forked = false;
pid_t DataIntegrityTest::receiver_pid = -1;
int DataIntegrityTest::listen_sock = -1;

static void handle_error(bool condition, const char *msg)
{
    if (condition) {
        perror(msg);
        exit(EXIT_FAILURE);
    }
}

ssize_t read_bytes(int fd, void *buf, size_t count)
{
    ssize_t bytes_read = 0;
    while (bytes_read < (ssize_t)count) {
	int rc = read(fd, (char*)buf + bytes_read, 
		      count - bytes_read);
        handle_error(rc <= 0, "read");
	bytes_read += rc;
    }
    return bytes_read;
}

void 
DataIntegrityTest::setUp()
{
    int rc = -1;
    rc = system("ps aux | grep -v grep | grep conn_scout > /dev/null");
    if (rc != 0) {
        fprintf(stderr, "conn_scout is not running; please start it first.\n");
        exit(EXIT_FAILURE);
    }

    if (!forked) {
        receiver_pid = fork();
        forked = true;

        if (receiver_pid == 0) {
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
    }
    
    if (receiver_pid == 0) {
        startReceiver();
    } else {
        sleep(2);
        int status = 0;
        rc = waitpid(receiver_pid, &status, WNOHANG);
        if (rc < 0) {
            fprintf(stderr, "Failed to check receiver status! errno=%d\n", errno);
            exit(EXIT_FAILURE);
        } else if (rc == receiver_pid && WIFEXITED(status)) {
            fprintf(stderr, "Receiver died prematurely, exit code %d\n", status);
            exit(EXIT_FAILURE);
        }

        printf("Receiver started, pid=%d\n", receiver_pid);
        printf("Starting sender\n");
        startSender();
    }
}

void 
DataIntegrityTest::tearDown()
{
    if (receiver_pid == 0) {
        cmm_close(read_sock);
    } else {
        cmm_close(send_sock);
    }
}

void
DataIntegrityTest::startReceiver()
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
DataIntegrityTest::startSender()
{
    send_sock = cmm_socket(PF_INET, SOCK_STREAM, 0);
    handle_error(send_sock < 0, "cmm_socket");

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    int rc = inet_aton("127.0.0.1", &addr.sin_addr);
    handle_error(rc < 0, "inet_aton");
    addr.sin_port = htons(TEST_PORT);

    socklen_t addrlen = sizeof(addr);

    printf("Sender is connecting...\n");
    rc = cmm_connect(send_sock, (struct sockaddr*)&addr, addrlen);
    handle_error(rc < 0, "cmm_connect");

    printf("Sender is connected.\n");
}

void 
DataIntegrityTest::testRandomBytesReceivedCorrectly()
{
    if (receiver_pid == 0) {
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
        int rc = read_bytes(rand_fd, buf, bytes);
        if (rc != bytes) {
            fprintf(stderr, "Failed to read random bytes! rc=%d, errno=%d\n",
                    rc, errno);
        }
        CPPUNIT_ASSERT_EQUAL_MESSAGE("Reading bytes from /dev/urandom",
                                     bytes, rc);
        close(rand_fd);
        
        printf("Sending %d random bytes and digest\n", bytes);
        int nbytes = htonl(bytes);
        rc = cmm_send(send_sock, &nbytes, sizeof(nbytes), 0, 0, 0, NULL, NULL);
        CPPUNIT_ASSERT_EQUAL_MESSAGE("Sending message size", 
                                     (int)sizeof(nbytes), rc);
        
        rc = cmm_send(send_sock, buf, bytes, 0, 0, 0, NULL, NULL);
        CPPUNIT_ASSERT_EQUAL_MESSAGE("Sending message", bytes, rc);
        
        unsigned char my_digest[SHA_DIGEST_LENGTH];
        unsigned char *result = SHA1(buf, bytes, my_digest);
        CPPUNIT_ASSERT_EQUAL_MESSAGE("Sender: computing digest", 
                                     (unsigned char*)my_digest, result);
        
        rc = cmm_send(send_sock, my_digest, SHA_DIGEST_LENGTH, 0, 
                      0, 0, NULL, NULL);
        CPPUNIT_ASSERT_EQUAL_MESSAGE("Sending digest", SHA_DIGEST_LENGTH, rc);

        printf("Sender finished.\n");
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
DataIntegrityTest::receiverAssertIntsSorted(int nums[], size_t n)
{
    int rc = cmm_read(read_sock, nums, n*sizeof(int), NULL);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("Receiving integers",
                                 (int)(n*sizeof(int)), rc);
    
    for (size_t i = 0; i < n; ++i) {
        nums[i] = ntohl(nums[i]);
    }
    CPPUNIT_ASSERT_MESSAGE("Integers are sorted least-to-greatest",
                           is_sorted(nums, n));
}

void
DataIntegrityTest::testOrderingSimple()
{
    const size_t NUMINTS = 10;
    int nums[NUMINTS] = {
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9
    };

    if (receiver_pid == 0) {
        receiverAssertIntsSorted(nums, NUMINTS);
    } else {
        for (size_t i = 0; i < NUMINTS; i++) {
            nums[i] = htonl(nums[i]);
            int rc = cmm_send(send_sock, &nums[i], sizeof(nums[i]), 0,
                              0, 0, NULL, NULL);
            CPPUNIT_ASSERT_EQUAL_MESSAGE("Sending integers", 
                                         (int)sizeof(nums[i]), rc);
        }
    }
}

void
DataIntegrityTest::testOrderingReverse()
{
    const size_t NUMINTS = 10;
    int nums[NUMINTS] = {
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9
    };

    if (receiver_pid == 0) {
        receiverAssertIntsSorted(nums, NUMINTS);
    } else {
        irob_id_t irobs[NUMINTS];
        irobs[0] = begin_irob(send_sock, 0, NULL, 0, 0, NULL, NULL);
        CPPUNIT_ASSERT_MESSAGE("begin_irob succeeds", irobs[0] >= 0);
        for (size_t i = 1; i < NUMINTS; ++i) {
            irobs[i] = begin_irob(send_sock, 1, &irobs[i-1], 0, 0, NULL, NULL);
            CPPUNIT_ASSERT_MESSAGE("begin_irob succeeds", irobs[i] >= 0);
        }
        for (int i = (int)NUMINTS - 1; i >= 0; --i) {
            int rc = irob_send(irobs[i], (char*)&irobs[i], sizeof(irobs[i]), 0);
            CPPUNIT_ASSERT_EQUAL_MESSAGE("irob_send succeeds", 
                                         (int)sizeof(irobs[i]), rc);
            rc = end_irob(irobs[i]);
            CPPUNIT_ASSERT_EQUAL_MESSAGE("end_irob succeeds", 0, rc);
        }
    }
}
