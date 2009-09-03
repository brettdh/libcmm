#include <cppunit/extensions/HelperMacros.h>
#include "end_to_end_tests_remote.h"
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

CPPUNIT_TEST_SUITE_REGISTRATION(EndToEndTestsRemote);

bool EndToEndTestsRemote::is_receiver = false;

extern bool g_receiver;
extern char *g_hostname;

void
EndToEndTestsRemote::chooseRole()
{
    // from command line, in run_all_tests.cpp
    is_receiver = g_receiver;
    setRemoteHost(g_hostname);
}

bool
EndToEndTestsRemote::isReceiver()
{
    return is_receiver;
}

void 
EndToEndTestsRemote::testDefaultIROBOrdering()
{
    const size_t NUMINTS = 10;
    int nums[NUMINTS] = {
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9
    };

    if (isReceiver()) {
        receiverAssertIntsSorted(nums, NUMINTS);
    } else {
        for (size_t i = 0; i < NUMINTS; ++i) {
            u_long label = ((i % 2 == 0) 
                            ? CMM_LABEL_ONDEMAND 
                            : CMM_LABEL_BACKGROUND);
            nums[i] = htonl(nums[i]);
            int rc = cmm_send(send_sock, &nums[i], sizeof(nums[i]), 0,
                              label, NULL, NULL);
            CPPUNIT_ASSERT_EQUAL_MESSAGE("Sending integers", 
                                         (int)sizeof(nums[i]), rc);

        }
    }
}

struct thunk_args {
    mc_socket_t sock;
    int *nums;
    size_t n;
    size_t next;
    pthread_mutex_t mutex;
    pthread_cond_t cv;

    thunk_args(mc_socket_t sock_, int *nums_, size_t n_, size_t next_)
        : sock(sock_), nums(nums_), n(n_), next(next_) {
        pthread_mutex_init(&mutex, NULL);
        pthread_cond_init(&cv, NULL);
    }
};

static void thunk_fn(void *arg)
{
    struct thunk_args *th_arg = (struct thunk_args*)arg;
    assert(th_arg);

    PthreadScopedLock lock(&th_arg->mutex);

    int rc = 0;
    while (rc == 0 && th_arg->next < th_arg->n) {
        rc = cmm_send(th_arg->sock, 
                      &th_arg->nums[th_arg->next], sizeof(int), 0, 
                      CMM_LABEL_BACKGROUND, thunk_fn, arg);
        if (rc >= 0) {
            CPPUNIT_ASSERT_EQUAL_MESSAGE("Integer sent",
                                         (int)sizeof(int), rc);
            th_arg->next++;
            rc = 0;
            sleep(1);
        } else {
            CPPUNIT_ASSERT_EQUAL_MESSAGE("Deferred, not failed",
                                         CMM_DEFERRED, rc);
        }
    }

    if (rc == 0) {
        assert(th_arg->next == th_arg->n);
        pthread_cond_signal(&th_arg->cv);
    }
}

void
EndToEndTestsRemote::testThunks()
{
    const size_t NUMINTS = 10;
    int nums[NUMINTS] = {
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9
    };

    if (isReceiver()) {
        receiverAssertIntsSorted(nums, NUMINTS);
    } else {
        for (size_t i = 0; i < NUMINTS; ++i) {
            nums[i] = htonl(nums[i]);
        }
        struct thunk_args *th_arg = new struct thunk_args(send_sock, nums, 
                                                          NUMINTS, 0);
        thunk_fn(th_arg);
        PthreadScopedLock lock(&th_arg->mutex);
        while (th_arg->next < th_arg->n) {
            pthread_cond_wait(&th_arg->cv, &th_arg->mutex);
        }
    }
}
