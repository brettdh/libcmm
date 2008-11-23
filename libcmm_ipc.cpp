#include <stdio.h>
#include <errno.h>
#include <mqueue.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include "libcmm_ipc.h"

#include "tbb/spin_mutex.h"
using tbb::spin_mutex;

typedef spin_mutex MsgMutexType;

static MsgMutexType msg_lock;

/* per-process message queue with the scout. */
static mqd_t scout_mq_fd;

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

void scout_ipc_init(int cmm_signal)
{
    int rc;
    struct cmm_msg msg;

    int len = snprintf(mq_name, MAX_PROC_MQ_NAMELEN-1, 
		       SCOUT_PROC_MQ_NAME_FMT, getpid());
    assert(len>0);
    mq_name[len] = '\0';

    struct mq_attr attr;
    attr.mq_flags = 0;
    attr.mq_maxmsg = 10;
    attr.mq_msgsize = sizeof(struct cmm_msg);
    mq_unlink(mq_name);
    scout_mq_fd = mq_open(mq_name, O_CREAT|O_RDWR,
			  0755, &attr);
    if (scout_mq_fd < 0) {
	fprintf(stderr, "Failed to open process message queue! errno=%d\n",
		errno);
	return;
    }

#if 0
    struct sigevent sigev;
    memset(&sigev, 0, sizeof(sigev));
    sigev.sigev_notify = SIGEV_SIGNAL;
    sigev.sigev_signo = cmm_signal;
    if (mq_notify(scout_mq_fd, &sigev) < 0) {
	fprintf(stderr, 
		"Failed to register signal handler for message queue, "
		"errno=%d\n", errno);
	mq_close(scout_mq_fd);
	scout_mq_fd = -1;
	return;
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
    }
}

void scout_ipc_deinit(void)
{
    if (scout_mq_fd > 0) {
	struct cmm_msg msg;
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

static u_long labels_available;

int scout_net_available(u_long labels)
{
    return (labels_available & labels);
}

u_long scout_receive_label_update(void);

void scout_request_update()
{
    struct cmm_msg msg;
    msg.opcode = CMM_MSG_UPDATE_STATUS;
    msg.data.pid = getpid();
    int rc = send_control_message(&msg);
    if (rc < 0) {
	fprintf(stderr, "Failed to send status update request\n");
    }
}

/* only call when a message is definitely present. */
u_long scout_receive_label_update()
{
    struct cmm_msg msg;
    memset(&msg, 0, sizeof(msg));
    scout_recv(&msg);

    if (msg.opcode != CMM_MSG_NET_STATUS_CHANGE) {
	fprintf(stderr, "Unexpected message opcode %d from conn scout\n",
		msg.opcode);
	return 0;
    }
    labels_available = msg.data.labels;
    return labels_available;
}
