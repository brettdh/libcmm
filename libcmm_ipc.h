#ifndef LIBCMM_IPC_H_INCL
#define LIBCMM_IPC_H_INCL

#include <sys/types.h>
#include <netinet/in.h>
#include <vector>
#include <map>

/* single message queue for subscribing and unsubscribing. */
#define SCOUT_CONTROL_MQ_NAME "/scout_control_mq"

/* per-process message queue identified by pid */
#define SCOUT_PROC_MQ_NAME_FMT "/scout_mq_proc_%d"
#define MAX_PROC_MQ_NAMELEN 101

void scout_ipc_init(int wakeup_sig);
void scout_ipc_deinit(void);
//bool scout_net_available(u_long send_labels, u_long recv_labels);
//void scout_request_update();

//void scout_labels_changed(u_long *new_up_labels, u_long *new_down_labels);

typedef enum {
    CMM_MSG_SUBSCRIBE=1,
    CMM_MSG_UNSUBSCRIBE,
    CMM_MSG_IFACE_LABELS, /* add/update an interface */
    CMM_MSG_IFACE_DOWN /* remove an interface */
} MsgOpcode;

struct cmm_msg {
    MsgOpcode opcode;
    union {
	pid_t pid;
        struct net_interface iface;
    } data;
};

#endif /* LIBCMM_IPC_H_INCL */
