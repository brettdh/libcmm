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
    CMM_MSG_IFACE_DOWN /* remove an interface */
} MsgOpcode;

struct cmm_msg {
    MsgOpcode opcode;
    union {
        pid_t pid;
        struct net_interface iface;
    } data;
};

#ifdef MULTI_PROCESS_SUPPORT
#include <boost/interprocess/containers/map.hpp>
#include <boost/interprocess/allocators/allocator.hpp>
#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/interprocess/smart_ptr/shared_ptr.hpp>
#include <glib.h>

struct fg_iface_data {
    gint last_fg_tv_sec; // last fg data on this iface, in epoch-seconds
    gint num_fg_senders; // number of processes with unACK'd FG data.
};

typedef boost::interprocess::managed_shared_ptr<struct fg_iface_data> FGDataPtr;
typedef boost::interprocess::map ShmemMap;
typedef ShmemMap<struct in_addr, FGDataPtr> FGDataMap;
#endif

#endif /* LIBCMM_IPC_H_INCL */
