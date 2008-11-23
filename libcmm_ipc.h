#ifndef LIBCMM_IPC_H_INCL
#define LIBCMM_IPC_H_INCL

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* single message queue for subscribing and unsubscribing. */
#define SCOUT_CONTROL_MQ_NAME "/scout_control_mq"

/* per-process message queue identified by pid */
#define SCOUT_PROC_MQ_NAME_FMT "/scout_mq_proc_%d"
#define MAX_PROC_MQ_NAMELEN 101

void scout_ipc_init(int wakeup_sig);
void scout_ipc_deinit(void);
int scout_net_available(u_long labels);
void scout_request_update();

/* only call when a message is definitely present. */
u_long scout_receive_label_update();

typedef enum {
    CMM_MSG_SUBSCRIBE=1,
    CMM_MSG_UNSUBSCRIBE,
    CMM_MSG_UPDATE_STATUS,
    CMM_MSG_NET_STATUS_CHANGE
} MsgOpcode;

struct cmm_msg {
    MsgOpcode opcode;
    union {
	pid_t pid;
	u_long labels;
	u_long available;
    } data;
};

#ifdef __cplusplus
}
#endif

#endif /* LIBCMM_IPC_H_INCL */
