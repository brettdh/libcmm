#include <cppunit/Test.h>
#include <cppunit/TestAssert.h>
#include "shmem_test.h"
#include "shmem_test_common.h"
#include "libcmm_shmem.h"
#include <sys/socket.h>
#include <linux/un.h>
#include <sys/wait.h>
#include "test_common.h"
#include <map>
using std::map;
#include "../csocket.h"

CPPUNIT_TEST_SUITE_REGISTRATION(ShmemTest);

const char *helper_filename = "./shmem_test_helper";

static struct init_once {
    init_once() {
        ipc_shmem_init(true);
    }
    ~init_once() {
        ipc_shmem_deinit();
    }
} once;

void ShmemTest::setUp()
{
    csocket1.reset(new CSocket(iface1, iface2));
    csocket2.reset(new CSocket(iface1, iface3));
}

void 
ShmemTest::testGlobalBufferCount()
{
    ipc_add_iface_pair(iface_pair(csocket1->local_iface.ip_addr,
                                  csocket1->remote_iface.ip_addr));

    int count = 5;

    size_t bytes = ipc_total_bytes_inflight(csocket1);
    CPPUNIT_ASSERT_MESSAGE("No sockets, no bytes", bytes == 0);

    printf("Forking children processes...");
    for (int i = 0; i < count; ++i) {
        int rc;
        pid_t child = fork();
        if (child == 0) {
            rc = execl(helper_filename, helper_filename,
                       "test_global_buffer_count", NULL);
            // shouldn't return

            handle_error(rc == -1, "execl");
        }
    }

    bytes = ipc_total_bytes_inflight(csocket1);
    CPPUNIT_ASSERT_MESSAGE("Newly created sockets should have empty buffers",
                           bytes == 0);
    printf("Waiting 8 seconds for socket buffers to fill...\n");
    struct timespec dur = {8, 0};
    nowake_nanosleep(&dur);
    printf("Checking global socket buffer count...\n");
    bytes = ipc_total_bytes_inflight(csocket1);
    printf("Total bytes for %d senders: %zu\n", count, bytes);
    CPPUNIT_ASSERT_MESSAGE("Full buffers should add to byte count",
                           bytes > 0);

    for (int i = 0; i < count; ++i) {
        int status = -1;
        pid_t pid = wait(&status);
        CPPUNIT_ASSERT(pid > 0);
        CPPUNIT_ASSERT_EQUAL(0, status);
    }
    
    ipc_remove_iface_pair(iface_pair(csocket1->local_iface.ip_addr,
                                     csocket1->remote_iface.ip_addr));
}

void
ShmemTest::testMap()
{
    ipc_add_iface_pair(iface_pair(csocket1->local_iface.ip_addr,
                                  csocket1->remote_iface.ip_addr));
    CPPUNIT_ASSERT_EQUAL(0, ipc_last_fg_tv_sec(csocket1));
    ipc_update_fg_timestamp(iface_pair(csocket1->local_iface.ip_addr,
                                       csocket1->remote_iface.ip_addr));
    CPPUNIT_ASSERT(ipc_last_fg_tv_sec(csocket1) > 0);

    ipc_add_iface_pair(iface_pair(csocket2->local_iface.ip_addr,
                                  csocket2->remote_iface.ip_addr));
    CPPUNIT_ASSERT_EQUAL(0, ipc_last_fg_tv_sec(csocket2));
    ipc_update_fg_timestamp(iface_pair(csocket2->local_iface.ip_addr,
                                       csocket2->remote_iface.ip_addr));
    CPPUNIT_ASSERT(ipc_last_fg_tv_sec(csocket2) > 0);

    ipc_remove_iface_pair(iface_pair(csocket1->local_iface.ip_addr,
                                     csocket1->remote_iface.ip_addr));
    ipc_add_iface_pair(iface_pair(csocket1->local_iface.ip_addr,
                                  csocket1->remote_iface.ip_addr));
    CPPUNIT_ASSERT_EQUAL(0, ipc_last_fg_tv_sec(csocket1));

    ipc_remove_iface_pair(iface_pair(csocket2->local_iface.ip_addr,
                                     csocket2->remote_iface.ip_addr));
    ipc_add_iface_pair(iface_pair(csocket1->local_iface.ip_addr,
                                  csocket1->remote_iface.ip_addr));
    CPPUNIT_ASSERT_EQUAL(0, ipc_last_fg_tv_sec(csocket1));

    ipc_remove_iface_pair(iface_pair(csocket1->local_iface.ip_addr,
                                     csocket1->remote_iface.ip_addr));
    ipc_remove_iface_pair(iface_pair(csocket2->local_iface.ip_addr,
                                     csocket2->remote_iface.ip_addr));
}
