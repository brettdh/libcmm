#ifndef LIBCMM_SHMEM_H_INCL
#define LIBCMM_SHMEM_H_INCL

#ifdef MULTI_PROCESS_SUPPORT
#include <boost/interprocess/containers/map.hpp>
#include <boost/interprocess/allocators/allocator.hpp>
#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/interprocess/smart_ptr/shared_ptr.hpp>
#include <glib.h>

#include <arpa/inet.h>
#include <netinet/in.h>

struct fg_iface_data {
    volatile gint last_fg_tv_sec; // last fg data on this iface, in epoch-seconds
    volatile gint num_fg_senders; // number of processes with unACK'd FG data.
};

typedef boost::interprocess::managed_shared_memory ManagedShmem;
typedef boost::interprocess::managed_shared_ptr<struct fg_iface_data,
                                                ManagedShmem> FGDataPtr;

typedef ManagedShmem::segment_manager MemMgr;
typedef boost::interprocess::allocator<std::pair<struct in_addr, FGDataPtr>, 
                                       MemMgr> FGDataAllocator;

typedef boost::interprocess::map<struct in_addr, FGDataPtr,
                                 FGDataAllocator> FGDataMap;

#define INTNW_SHMEM_NAME "IntNWSharedSegment"
#define INTNW_SHMEM_SIZE 4096
#define INTNW_SHMEM_MAP_NAME "IntNWSharedMap"
#define INTNW_SHMEM_MUTEX_NAME "IntNWSharedMutex"

void ipc_shmem_init(bool create);
void ipc_shmem_deinit();

gint ipc_last_fg_tv_sec(struct in_addr ip_addr);
gint ipc_fg_sender_count(struct in_addr ip_addr);
void ipc_update_fg_timestamp(struct in_addr ip_addr);
void ipc_increment_fg_senders(struct in_addr ip_addr);
void ipc_decrement_fg_senders(struct in_addr ip_addr);

void add_iface(struct in_addr ip_addr);
void remove_iface(struct in_addr ip_addr);
#endif

#endif
