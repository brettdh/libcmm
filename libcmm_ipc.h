#ifndef LIBCMM_IPC_H_INCL
#define LIBCMM_IPC_H_INCL

#include <sys/types.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <vector>
#include <map>
#include "common.h"
#include "net_interface.h"

/* single message queue for subscribing and unsubscribing. */
#define SCOUT_CONTROL_MQ_NAME "/scout_control_mq"

/* per-process message queue identified by pid */
#define SCOUT_PROC_MQ_NAME_FMT "/scout_mq_proc_%d"
#define MAX_PROC_MQ_NAMELEN 101

#define SCOUT_PROC_MQ_MODE (S_IRUSR | S_IWUSR | S_IRGRP | \
                            S_IWGRP | S_IROTH | S_IWOTH)

void scout_ipc_init(void);
void scout_ipc_deinit(void);

bool scout_ipc_inited(void);

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
