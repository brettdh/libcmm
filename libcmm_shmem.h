#ifndef LIBCMM_SHMEM_H_INCL
#define LIBCMM_SHMEM_H_INCL

#ifdef MULTI_PROCESS_SUPPORT
#include <boost/interprocess/containers/set.hpp>
#include <boost/interprocess/containers/map.hpp>
#include <boost/interprocess/allocators/allocator.hpp>
#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/interprocess/smart_ptr/shared_ptr.hpp>
#include <glib.h>

#include <arpa/inet.h>
#include <netinet/in.h>

struct fg_iface_data {
    volatile gint last_fg_tv_sec; // last fg data on this iface, in epoch-seconds
    //volatile gint num_fg_senders; // number of processes with unACK'd FG data.
};

typedef boost::interprocess::managed_shared_memory ManagedShmem;
typedef ManagedShmem::segment_manager MemMgr;

typedef boost::interprocess::managed_shared_ptr<struct fg_iface_data,
                                                ManagedShmem>::type FGDataPtr;

typedef boost::interprocess::allocator<std::pair<struct in_addr, FGDataPtr>, 
                                       MemMgr> FGDataAllocator;

struct in_addr_less {
    bool operator()(const struct in_addr& addr1,
                    const struct in_addr& addr2) const {
        return addr1.s_addr < addr2.s_addr;
    }
};

typedef boost::interprocess::map<struct in_addr, FGDataPtr, in_addr_less,
                                 FGDataAllocator> FGDataMap;

typedef boost::interprocess::allocator<int, MemMgr> IntAllocator;
typedef boost::interprocess::set<int, std::less<int>, IntAllocator> ShmemIntSet;

#define INTNW_SHMEM_NAME "IntNWSharedSegment"
#define INTNW_SHMEM_SIZE 65536
#define INTNW_SHMEM_MAP_NAME "IntNWLocalIfaceMap"
#define INTNW_SHMEM_MUTEX_NAME "IntNWSharedMutex"
#define INTNW_SHMEM_PID_SET_NAME "IntNWPIDSet"

void ipc_shmem_init(bool create);
void ipc_shmem_deinit();

gint ipc_last_fg_tv_sec(struct in_addr ip_addr);
//gint ipc_fg_sender_count(struct in_addr ip_addr);
void ipc_update_fg_timestamp(struct in_addr ip_addr);
void ipc_set_last_fg_tv_sec(struct in_addr ip_addr, gint secs);
//void ipc_increment_fg_senders(struct in_addr ip_addr);
//void ipc_decrement_fg_senders(struct in_addr ip_addr);

// call when there are now no FG IROBs in flight
//void ipc_decrement_all_fg_senders();
size_t ipc_total_bytes_inflight(struct in_addr ip_addr);

bool ipc_add_iface(struct in_addr ip_addr);
bool ipc_remove_iface(struct in_addr ip_addr);
bool ipc_add_csocket(struct in_addr ip_addr, int local_fd);
bool ipc_remove_csocket(struct in_addr ip_addr, int local_fd);

#endif

#endif
