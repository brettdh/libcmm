#include <cppunit/extensions/HelperMacros.h>
#include "trickle_tests.h"
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <openssl/sha.h>
#include <assert.h>
#include <libcmm.h>
#include <libcmm_irob.h>

#include <pthread.h>
#include "../pthread_util.h"

CPPUNIT_TEST_SUITE_REGISTRATION(TrickleTests);

const size_t NUMINTS = 20;

struct thread_args {
    mc_socket_t sock;
    u_long send_label;
    struct timespec delay;
    int nums[NUMINTS];
};

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

static void nowake_nanosleep(const struct timespec *duration)
{
    struct timespec time_left;
    struct timespec sleep_time = *duration;
    while (nanosleep(&sleep_time, &time_left) < 0) {
        if (errno != EINTR) {
            perror("nanosleep");
            exit(-1);
        }
        sleep_time = time_left;
    }
}

void *SenderThread(void *arg)
{
    struct thread_args *th_arg = (struct thread_args *)arg;
    
    int numdeps = 0;
    irob_id_t *deps = NULL;
    irob_id_t last_id = -1;
    size_t next = 0;
    while (next < NUMINTS) {
        nowake_nanosleep(&th_arg->delay);
        
        PthreadScopedLock lock(&mutex);
        fprintf(stderr, "Sending %s int...\n",
                th_arg->send_label == CMM_LABEL_ONDEMAND ? "FG" : "BG");

        irob_id_t id = begin_irob(th_arg->sock, numdeps, deps, 
                                  th_arg->send_label, NULL, NULL);
        CPPUNIT_ASSERT_MESSAGE("begin_irob succeeds", id >= 0);
        last_id = id;
        deps = &last_id;
        numdeps = 1;

        int rc= irob_send(id, &th_arg->nums[next], 
                          sizeof(int), 0);
        if (rc >= 0) {
            fprintf(stderr, "...sent, rc=%d\n", rc);
            CPPUNIT_ASSERT_EQUAL_MESSAGE("Integer sent",
                                         (int)sizeof(int), rc);
            rc = end_irob(id);
            CPPUNIT_ASSERT_EQUAL_MESSAGE("IROB completed",
                                         0, rc);
            next++;
        } else {
            fprintf(stderr, "failed! rc=%d, errno=%d\n", rc, errno);
            CPPUNIT_FAIL("Failed to send integer");
        }
    }
    
    delete th_arg;
    return NULL;
}

void 
TrickleTests::testTrickle()
{
    int nums[NUMINTS * 2];
    for (size_t i = 0; i < NUMINTS; ++i) {
        nums[i] = i;
        nums[i + NUMINTS] = i + 1000;
    }

    if (isReceiver()) {
        receiverAssertIntsSorted(nums, NUMINTS*2);
    } else {
        struct thread_args *fg_args = new struct thread_args;
        fg_args->sock = send_sock;
        fg_args->send_label = CMM_LABEL_ONDEMAND;
        fg_args->delay.tv_sec = 0;
        fg_args->delay.tv_nsec = 700000000;

        struct thread_args *bg_args = new struct thread_args;
        bg_args->sock = send_sock;
        bg_args->send_label = CMM_LABEL_BACKGROUND;
        bg_args->delay.tv_sec = 2;
        bg_args->delay.tv_nsec = 0;

        for (size_t i = 0; i < NUMINTS; ++i) {
            fg_args->nums[i] = htonl(nums[i]);
            bg_args->nums[i] = htonl(nums[i + NUMINTS]);
        }

        pthread_t fg_thread, bg_thread;
        int rc = pthread_create(&fg_thread, NULL, SenderThread, fg_args);
        CPPUNIT_ASSERT_EQUAL_MESSAGE("Created FG thread", 0, rc);
        sleep(1);
        rc = pthread_create(&bg_thread, NULL, SenderThread, bg_args);
        CPPUNIT_ASSERT_EQUAL_MESSAGE("Created BG thread", 0, rc);

        pthread_join(fg_thread, NULL);
        pthread_join(bg_thread, NULL);
    }
}
