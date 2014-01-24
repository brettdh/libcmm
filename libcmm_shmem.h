#ifndef LIBCMM_SHMEM_H_INCL
#define LIBCMM_SHMEM_H_INCL

#ifdef ANDROID
/* TODO: use Android's /dev/ashmem and IBinder interfaces
 *       to do the shared memory stuff. 
 */
#else
#include <boost/interprocess/containers/set.hpp>
#include <boost/interprocess/containers/map.hpp>
#include <boost/interprocess/allocators/allocator.hpp>
#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/interprocess/smart_ptr/shared_ptr.hpp>
#endif

#include <atomic>

#include "debug.h"
#include "common.h"

#include <arpa/inet.h>
#include <netinet/in.h>

#include <iostream>

#ifdef ANDROID
/* TODO: use Android's /dev/ashmem and IBinder interfaces
 *       to do the shared memory stuff. 
 */
#else
struct fg_iface_data {
    std::atomic<int> last_fg_tv_sec; // last fg data on this iface, in epoch-seconds
    //std::atomic<int> num_fg_senders; // number of processes with unACK'd FG data.
};

typedef boost::interprocess::managed_shared_memory ManagedShmem;
typedef ManagedShmem::segment_manager MemMgr;

typedef boost::interprocess::managed_shared_ptr<struct fg_iface_data,
                                                ManagedShmem>::type FGDataPtr;

typedef boost::interprocess::allocator<std::pair<const struct iface_pair, FGDataPtr>, 
                                       MemMgr> FGDataAllocator;
#endif /* !ANDROID */

struct iface_pair {
    struct in_addr local_iface;
    struct in_addr remote_iface;

    iface_pair() {
        memset(&local_iface, 0, sizeof(local_iface));
        memset(&remote_iface, 0, sizeof(remote_iface));
    }
    iface_pair(struct in_addr local, struct in_addr remote)
        : local_iface(local), remote_iface(remote) {}

    bool operator<(const struct iface_pair& other) const {
        return (local_iface.s_addr < other.local_iface.s_addr ||
                (local_iface.s_addr == other.local_iface.s_addr &&
                 remote_iface.s_addr < other.remote_iface.s_addr));
    }
    bool operator==(const struct iface_pair& other) const {
        return (local_iface.s_addr == other.local_iface.s_addr &&
                remote_iface.s_addr == other.remote_iface.s_addr);
    }

    void print(std::ostream& out) {
        out << "(" << StringifyIP(&local_iface).c_str()
            << ", " << StringifyIP(&remote_iface).c_str() << ")";
    }
};

struct in_addr_less {
    bool operator()(const struct in_addr& addr1,
                    const struct in_addr& addr2) const {
        return addr1.s_addr < addr2.s_addr;
    }
};

#ifdef ANDROID
/* TODO: use Android's /dev/ashmem and IBinder interfaces
 *       to do the shared memory stuff. 
 */
#else
typedef boost::interprocess::map<struct iface_pair, FGDataPtr, std::less<struct iface_pair>,
                                 FGDataAllocator> FGDataMap;

typedef boost::interprocess::allocator<int, MemMgr> IntAllocator;
typedef boost::interprocess::set<int, std::less<int>, IntAllocator> ShmemIntSet;

#define INTNW_SHMEM_NAME "IntNWSharedSegment"
#define INTNW_SHMEM_SIZE 65536
#define INTNW_SHMEM_MAP_NAME "IntNWLocalIfaceMap"
#define INTNW_SHMEM_MUTEX_NAME "IntNWSharedMutex"
#define INTNW_SHMEM_PID_SET_NAME "IntNWPIDSet"
#endif /* !ANDROID */

void ipc_shmem_init(bool create);
void ipc_shmem_deinit();

#ifndef BUILDING_SCOUT
#include "csocket.h"
// the scout doesn't care about these functions, and it
//  certainly knows nothing about CSockets.
int ipc_last_fg_tv_sec(CSocketPtr csock); //struct in_addr ip_addr);
//int ipc_fg_sender_count(struct in_addr ip_addr);
void ipc_update_fg_timestamp(struct iface_pair ifaces);//struct in_addr ip_addr);
void ipc_set_last_fg_tv_sec(struct iface_pair ifaces, //struct in_addr ip_addr,
                            int secs);
//void ipc_increment_fg_senders(struct in_addr ip_addr);
//void ipc_decrement_fg_senders(struct in_addr ip_addr);

// call when there are now no FG IROBs in flight
//void ipc_decrement_all_fg_senders();
size_t ipc_total_bytes_inflight(CSocketPtr csock);//struct in_addr ip_addr);

bool ipc_add_csocket(CSocketPtr csock, //struct in_addr ip_addr,
                     int local_fd);
bool ipc_remove_csocket(struct iface_pair ifaces, //struct in_addr ip_addr, 
                        int local_fd);
#endif

bool ipc_add_iface_pair(struct iface_pair ifaces);
bool ipc_remove_iface_pair(struct iface_pair ifaces);

#endif
