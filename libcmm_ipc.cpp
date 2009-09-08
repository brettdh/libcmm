#include <stdio.h>
#include <errno.h>
#include <mqueue.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <signal.h>
#include "libcmm_ipc.h"
#include "cmm_thread.h"
#include <set>
using std::set;

#include "tbb/spin_mutex.h"
using tbb::spin_mutex;

typedef spin_mutex MsgMutexType;

static MsgMutexType msg_lock;

/* per-process message queue with the scout. */
static mqd_t scout_mq_fd;

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
    struct timespec timeout = {1, 0}; /* 1-second timeout */    

  try_receive:
    rc = mq_timedreceive(scout_mq_fd, (char*)msg, len, NULL, &timeout);
    if (rc != len) {
	if (errno == EINTR || errno == ETIMEDOUT) {
	    goto try_receive;
	}
	fprintf(stderr, 
		"Receiving response from conn scout failed! rc=%d, errno=%d\n",
		rc, errno);
	return rc;
    }

    return rc;
}

static int send_control_message(const struct cmm_msg *msg)
{
    mqd_t scout_control_mq = mq_open(SCOUT_CONTROL_MQ_NAME, O_WRONLY);
    if (scout_control_mq < 0) {
	perror("mq_open");
	return scout_control_mq;
    }

    int rc;
    struct timespec timeout = {1, 0}; /* 1-second timeout */    

  try_send:
    rc = mq_timedsend(scout_control_mq, (char*)msg, sizeof(*msg), 0, &timeout);
    if (rc < 0) {
	if (errno == EINTR || errno == ETIMEDOUT) {
	    goto try_send;
	}
	perror("mq_timedsend");
	return rc;
    }

    mq_close(scout_control_mq);

    return 0;
}

static char mq_name[MAX_PROC_MQ_NAMELEN];

#if 0
static struct sigaction old_action;
static struct sigaction ignore_action;
static struct sigaction net_status_change_action;
#endif

static void net_status_change_handler();
static void *IPCThread(void*);

bool scout_ipc_inited(void)
{
    return (scout_mq_fd > 0);
}

void scout_ipc_init()
{
    int rc;
    struct cmm_msg msg;
    memset(&msg, 0, sizeof(msg));

    int len = snprintf(mq_name, MAX_PROC_MQ_NAMELEN-1, 
		       SCOUT_PROC_MQ_NAME_FMT, getpid());
    assert(len>0);
    mq_name[len] = '\0';

    struct mq_attr attr;
    attr.mq_flags = 0;
    attr.mq_maxmsg = 10;
    attr.mq_msgsize = sizeof(struct cmm_msg);
    mq_unlink(mq_name);
    mode_t default_mode = umask(0);
    scout_mq_fd = mq_open(mq_name, O_CREAT|O_RDWR,
			  SCOUT_PROC_MQ_MODE, &attr);
    (void)umask(default_mode);
    if (scout_mq_fd < 0) {
	fprintf(stderr, "Failed to open process message queue! errno=%d\n",
		errno);
	return;
    }

#if 0
    memset(&ignore_action, 0, sizeof(ignore_action));
    memset(&net_status_change_action, 0, sizeof(net_status_change_action));
    memset(&old_action, 0, sizeof(old_action));

    ignore_action.sa_handler = SIG_IGN;
    net_status_change_action.sa_handler = net_status_change_handler;

    sigaction(cmm_signal, &net_status_change_action, &old_action);
    if (old_action.sa_handler != SIG_DFL) {
	/* Unclear that this would ever happen, as this lib is probably
	 * loaded before the app registers a signal handler of its own.
	 * This places the burden on the app developer to avoid colliding
	 * with our signal of choice. */
	fprintf(stderr, 
		"WARNING: the application has changed the "
		"default handler for signal %d\n", cmm_signal);
    }
#endif
    
    msg.opcode = CMM_MSG_SUBSCRIBE;
    msg.data.pid = getpid();
    rc = send_control_message(&msg);
    if (rc < 0) {
	fprintf(stderr, 
		"Failed to send subscription message to scout, "
		"errno=%d\n", errno);
	mq_close(scout_mq_fd);
	scout_mq_fd = -1;
	mq_unlink(mq_name);
	mq_name[0] = '\0';
    } else {
        rc = pthread_create(&ipc_thread_id, NULL, IPCThread, NULL);
    }
}

void scout_ipc_deinit(void)
{
    if (scout_mq_fd > 0) {
	struct cmm_msg msg;
        memset(&msg, 0, sizeof(msg));
	msg.opcode = CMM_MSG_UNSUBSCRIBE;
	msg.data.pid = getpid();
	int rc = send_control_message(&msg);
	if (rc < 0) {
	    fprintf(stderr, "Warning: failed to send unsubscribe message\n");
	}
	mq_close(scout_mq_fd);
	mq_unlink(mq_name);
    }
}

extern void process_interface_update(struct net_interface iface, bool down);

/* only call when a message is definitely present. */
static void net_status_change_handler(void)
{
    struct cmm_msg msg;
    memset(&msg, 0, sizeof(msg));
    scout_recv(&msg);

    switch (msg.opcode) {
    case CMM_MSG_IFACE_LABELS:
        process_interface_update(msg.data.iface, false);
        break;
    case CMM_MSG_IFACE_DOWN:
        process_interface_update(msg.data.iface, true);
        break;
    default:
	fprintf(stderr, "Unexpected message opcode %d from conn scout\n",
		msg.opcode);
	return;
    }
}

static void *IPCThread(void *arg)
{
    char name[MAX_NAME_LEN+1] = "IPCThread";
    set_thread_name(name);

    while (1) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(scout_mq_fd, &readfds);

        int rc = select(scout_mq_fd + 1, &readfds, NULL, NULL, NULL);
        if (rc < 0) {
            if (errno == EINTR) {
                continue;
            } else {
                break;
            }
        } else {
            net_status_change_handler();
        }
    }
    return NULL;
}
