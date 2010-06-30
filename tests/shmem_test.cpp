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

void 
ShmemTest::setUp()
{
    int rc;
    master_fd = socket(PF_UNIX, SOCK_DGRAM, 0);
    handle_error(master_fd < 0, "socket");

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(&addr.sun_path[1], MASTER_SOCKET_NAME, 
            UNIX_PATH_MAX - 2);
    rc = bind(master_fd, (struct sockaddr*)&addr,
              sizeof(addr));
    handle_error(rc < 0, "bind");
}

void 
ShmemTest::tearDown()
{
    close(master_fd);
}

void 
ShmemTest::testCount()
{
    ipc_add_iface(shmem_iface1);

    int count = 10;
    for (int i = 0; i < count; ++i) {
        char arg[10];
        sprintf(arg, "%d", i);

        int rc;
        pid_t child = fork();
        if (child == 0) {
            rc = execl(helper_filename, helper_filename,
                       "test_count", arg, NULL);
            // shouldn't return

            handle_error(true, "execl");
        } else {
            int nums[2] = {-1, -1};
            rc = read(master_fd, nums, sizeof(nums));
            CPPUNIT_ASSERT_EQUAL((int)sizeof(nums), rc);
            CPPUNIT_ASSERT_EQUAL(i, nums[0]);
            CPPUNIT_ASSERT_EQUAL(i, nums[1]);

            int status = 0;
            pid_t exiter = wait(&status);
            CPPUNIT_ASSERT_EQUAL(child, exiter);
            CPPUNIT_ASSERT_EQUAL(0, status);
        }
    }

    ipc_remove_iface(shmem_iface1);
}

void
ShmemTest::testRace()
{
    ipc_add_iface(shmem_iface1);
    ipc_add_iface(shmem_iface2);

    int count = 25;
    map<int, int> iface_counts;
    for (int i = 0; i < count; ++i) {
        char num_ops_arg[10];
        int num_ops = (rand() % 20000) + 100000;
        sprintf(num_ops_arg, "%d", num_ops);

        bool increment = ((rand() % 2) == 0);
        int iface_num = ((rand() % 2) + 1);
        char iface_num_arg[5];
        sprintf(iface_num_arg, "%d", iface_num);
        
        if (increment) {
            iface_counts[iface_num] += num_ops;
        } else {
            iface_counts[iface_num] -= num_ops;
        }

        int rc;
        pid_t child = fork();
        if (child == 0) {
            rc = execl(helper_filename, helper_filename,
                       "test_race", num_ops_arg, 
                       increment ? "increment" : "decrement",
                       iface_num_arg, NULL);
            // shouldn't return

            handle_error(true, "execl");
        }
    }

    for (int i = 0; i < 25; ++i) {
        int status = -1;
        pid_t pid = wait(&status);
        CPPUNIT_ASSERT(pid > 0);
        CPPUNIT_ASSERT_EQUAL(0, status);
    }

    CPPUNIT_ASSERT_EQUAL(iface_counts[1], ipc_fg_sender_count(shmem_iface1));
    CPPUNIT_ASSERT_EQUAL(iface_counts[2], ipc_fg_sender_count(shmem_iface2));
    
    ipc_remove_iface(shmem_iface2);
    ipc_remove_iface(shmem_iface1);
}

void
ShmemTest::testMap()
{
    ipc_add_iface(shmem_iface1);
    CPPUNIT_ASSERT_EQUAL(0, ipc_fg_sender_count(shmem_iface1));
    CPPUNIT_ASSERT_EQUAL(0, ipc_last_fg_tv_sec(shmem_iface1));
    ipc_increment_fg_senders(shmem_iface1);
    ipc_update_fg_timestamp(shmem_iface1);
    CPPUNIT_ASSERT_EQUAL(1, ipc_fg_sender_count(shmem_iface1));
    CPPUNIT_ASSERT(ipc_last_fg_tv_sec(shmem_iface1) > 0);

    ipc_add_iface(shmem_iface2);
    CPPUNIT_ASSERT_EQUAL(0, ipc_fg_sender_count(shmem_iface2));
    CPPUNIT_ASSERT_EQUAL(0, ipc_last_fg_tv_sec(shmem_iface2));
    ipc_increment_fg_senders(shmem_iface2);
    ipc_update_fg_timestamp(shmem_iface2);
    CPPUNIT_ASSERT_EQUAL(1, ipc_fg_sender_count(shmem_iface2));
    CPPUNIT_ASSERT(ipc_last_fg_tv_sec(shmem_iface2) > 0);

    ipc_remove_iface(shmem_iface1);
    ipc_add_iface(shmem_iface1);
    CPPUNIT_ASSERT_EQUAL(0, ipc_fg_sender_count(shmem_iface1));
    CPPUNIT_ASSERT_EQUAL(0, ipc_last_fg_tv_sec(shmem_iface1));

    ipc_remove_iface(shmem_iface2);
    ipc_add_iface(shmem_iface2);
    CPPUNIT_ASSERT_EQUAL(0, ipc_fg_sender_count(shmem_iface1));
    CPPUNIT_ASSERT_EQUAL(0, ipc_last_fg_tv_sec(shmem_iface1));

    ipc_remove_iface(shmem_iface1);
    ipc_remove_iface(shmem_iface2);
}
