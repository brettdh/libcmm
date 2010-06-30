#include "libcmm_shmem.h"
#include "shmem_test_common.h"
#include "debug.h"
#include <glib.h>
#include <stdio.h>
#include <sys/socket.h>
#include <linux/un.h>
#include "test_common.h"

int main(int argc, char *argv[])
{
    ipc_shmem_init(false);
    assert(argc >= 2);
    
    int master_fd = socket(PF_UNIX, SOCK_DGRAM, 0);
    if (master_fd < 0) {
        perror("socket");
        return -1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(&addr.sun_path[1], MASTER_SOCKET_NAME,
            UNIX_PATH_MAX - 2);
    int rc = connect(master_fd, (struct sockaddr *)&addr, sizeof(addr));
    if (rc < 0) {
        perror("connect");
        dbgprintf_always("Failed to connect to master IPC socket\n");
        close(master_fd);
        master_fd = -1;
        return -1;
    }

    const char *cmd = argv[1];
    if (!strcmp(cmd, "test_count")) {
        /* $prog test_count <proc_num> */
        assert(argc == 3);
        int proc_num = atoi(argv[2]);
        assert(proc_num >= 0);
        gint sender_count = ipc_fg_sender_count(shmem_iface1);
        if (proc_num != sender_count) {
            dbgprintf_always("[child %d] count test failed! "
                             "shmem count is %d; expected %d\n",
                             getpid(), sender_count, proc_num);
        } else {
            dbgprintf_always("[child %d] counter %d passed test\n", 
                    getpid(), proc_num);
        }
        ipc_increment_fg_senders(shmem_iface1);

        int nums[2] = { proc_num, sender_count };
        rc = write(master_fd, nums, sizeof(nums));
        handle_error(rc != sizeof(nums), "write");
    } else if (!strcmp(cmd, "test_race")) {
        /* $prog test_race <num_ops> <increment | decrement> <1 | 2> */
        assert(argc == 5);
        int num_ops = atoi(argv[2]);
        assert(num_ops > 0);
        void (*modify_count)(struct in_addr) = NULL;
        if (!strcmp(argv[3], "increment")) {
            modify_count = ipc_increment_fg_senders;
        } else if (!strcmp(argv[3], "decrement")) {
            modify_count = ipc_decrement_fg_senders;
        } else assert(0);

        struct in_addr iface;
        int iface_num = atoi(argv[4]);
        if (iface_num == 1) {
            iface = shmem_iface1;
        } else if (iface_num == 2) {
            iface = shmem_iface2;
        } else assert(0);

        dbgprintf_always("[child %d] %sing iface%d count %d times\n",
                         getpid(), argv[3], iface_num, num_ops);
        for (int i = 0; i < num_ops; ++i) {
            modify_count(iface);
        }
        dbgprintf_always("[child %d] Done.\n", getpid());
    } else {
        assert(0);
    }

    close(master_fd);

    return 0;
}
