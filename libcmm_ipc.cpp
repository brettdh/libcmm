#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <signal.h>
#include <sys/socket.h>
#include <linux/un.h>
#include "libcmm_ipc.h"
#include "cmm_thread.h"
#include <set>
using std::set;

#include "libcmm_shmem.h"


#define CMM_SELECT_SIGNAL 42 /* I am assured this is okay in Linux. */

#include "debug.h"

static bool running = true;

/* per-process message queue with the scout. */
static int scout_ipc_fd;

static pthread_t ipc_thread_id;

static int send_control_message(const struct cmm_msg *msg);
static int scout_recv(struct cmm_msg *msg);

/* Sends msg to the scout (on control msgqueue) 
 * and receives response (on proc-msgqueue) in same struct cmm_msg. */
#if 0
static int scout_twoway(struct cmm_msg *msg)
{
    int len, rc;
    struct timespec timeout = {1, 0}; /* 1-second timeout */

    rc = send_control_message(msg);

    memset(msg, 0, sizeof(*msg));

    rc = scout_recv(msg);

    return rc;
}
#endif

static int scout_recv(struct cmm_msg *msg)
{
    int rc;
    int len = sizeof(*msg);

  try_receive:
    rc = read(scout_ipc_fd, (char*)msg, len);
    if (rc != len) {
        if (rc < 0) {
            if (errno == EINTR) {
                goto try_receive;
            } else {
                perror("read");
            }
        }
        dbgprintf_always(
                "Receiving response from conn scout failed! rc=%d, errno=%d\n",
                rc, errno);
        return rc;
    }

    return rc;
}

static int send_control_message(const struct cmm_msg *msg)
{
  try_send:
    int rc = write(scout_ipc_fd, (char*)msg, sizeof(*msg));
    if (rc < 0) {
        if (errno == EINTR) {
            goto try_send;
        }
        perror("write");
    }

    return rc;
}

static int net_status_change_handler();
static void *IPCThread(void*);

bool scout_ipc_inited(void)
{
    return (scout_ipc_fd > 0);
}

int scout_ipc_init()
{
    ipc_shmem_init(false);

    struct cmm_msg msg;
    memset(&msg, 0, sizeof(msg));

    scout_ipc_fd = socket(PF_UNIX, SOCK_STREAM, 0);
    if (scout_ipc_fd < 0) {
        perror("socket");
        return -1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(&addr.sun_path[1], SCOUT_CONTROL_MQ_NAME,
            UNIX_PATH_MAX - 2);
    int rc = connect(scout_ipc_fd, (struct sockaddr *)&addr, sizeof(addr));
    if (rc < 0) {
        perror("connect");
        dbgprintf_always("Failed to connect to scout IPC socket\n");
        close(scout_ipc_fd);
        scout_ipc_fd = -1;
        return -1;
    }

    msg.opcode = CMM_MSG_SUBSCRIBE;
    msg.data.pid = getpid();
    rc = send_control_message(&msg);
    if (rc < 0) {
        dbgprintf_always(
                "Failed to send subscription message to scout, "
                "errno=%d\n", errno);
        close(scout_ipc_fd);
        scout_ipc_fd = -1;
    } else {
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        rc = pthread_create(&ipc_thread_id, &attr, IPCThread, NULL);
        if (rc != 0) {
            close(scout_ipc_fd);
            scout_ipc_fd = -1;
            dbgprintf_always("Failed to create IPC thread, rc=%d\n", rc);
            return -1;
        }
    }
    
    return 0;
}

void scout_ipc_deinit(void)
{
    if (scout_ipc_fd > 0) {
        struct cmm_msg msg;
        memset(&msg, 0, sizeof(msg));
        msg.opcode = CMM_MSG_UNSUBSCRIBE;
        msg.data.pid = getpid();
        int rc = send_control_message(&msg);
        if (rc < 0) {
            dbgprintf_always("Warning: failed to send unsubscribe message\n");
        }
        running = false;
        close(scout_ipc_fd);
        pthread_kill(ipc_thread_id, CMM_SELECT_SIGNAL);
    }

    ipc_shmem_deinit();
}

extern void process_interface_update(struct net_interface iface, bool down);

/* only call when a message is definitely present. */
static int net_status_change_handler(void)
{
    struct cmm_msg msg;
    memset(&msg, 0, sizeof(msg));
    int rc = scout_recv(&msg);
    if (rc != sizeof(msg)) {
        return -1;
    }

    switch (msg.opcode) {
    case CMM_MSG_IFACE_LABELS:
        process_interface_update(msg.data.iface, false);
        break;
    case CMM_MSG_IFACE_DOWN:
        process_interface_update(msg.data.iface, true);
        break;
    default:
        dbgprintf_always("Unexpected message opcode %d from conn scout\n",
                msg.opcode);
        break;
    }
    return 0;
}

static void sig_handler(int sig)
{
    running = false;
}

static void *IPCThread(void *arg)
{
    char name[MAX_NAME_LEN+1] = "IPCThread";
    set_thread_name(name);

    pthread_detach(pthread_self());
    
    set_signal(CMM_SELECT_SIGNAL, sig_handler);

    while (running) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(scout_ipc_fd, &readfds);

        int rc = select(scout_ipc_fd + 1, &readfds, NULL, NULL, NULL);
        if (rc < 0) {
            if (errno == EINTR) {
                continue;
            } else {
                break;
            }
        } else {
            if (net_status_change_handler() != 0) {
                break;
            }
        }
    }
    dbgprintf("IPC thread exiting.\n");
    return NULL;
}
