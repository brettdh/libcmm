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
#define SCOUT_CONTROL_MQ_NAME "scout_control_mq"

int scout_ipc_init(void);
void scout_ipc_deinit(void);

bool scout_ipc_inited(void);

typedef enum {
    CMM_MSG_SUBSCRIBE=1,
    CMM_MSG_UNSUBSCRIBE,
    CMM_MSG_IFACE_LABELS, /* add/update an interface */
    CMM_MSG_IFACE_DOWN, /* remove an interface */
    CMM_MSG_GET_IFACES /* get all interfaces plus a sentinel */
} MsgOpcode;

struct cmm_msg {
    MsgOpcode opcode;
    union {
        pid_t pid;
        struct net_interface iface;
    } data;
};

#endif /* LIBCMM_IPC_H_INCL */
