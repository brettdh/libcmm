#include "libcmm_shmem.h"
#include "timeops.h"
#include "common.h"

#ifdef MULTI_PROCESS_SUPPORT
#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/interprocess/sync/named_upgradable_mutex.hpp>
#include <boost/interprocess/sync/sharable_lock.hpp>
#include <boost/interprocess/sync/upgradable_lock.hpp>
#include <boost/interprocess/sync/scoped_lock.hpp>
using boost::interprocess::managed_shared_memory;
using boost::interprocess::shared_memory_object;
using boost::interprocess::make_managed_shared_ptr;
using boost::interprocess::create_only;
using boost::interprocess::open_only;
using boost::interprocess::named_upgradable_mutex;
using boost::interprocess::sharable_lock;
using boost::interprocess::upgradable_lock;
using boost::interprocess::scoped_lock;
using boost::interprocess::move;
using boost::interprocess::anonymous_instance;

#include <vector>
#include <map>
#include <set>
using std::make_pair;

#include <sys/socket.h>
#include <linux/un.h>
#include <ancillary.h>

static managed_shared_memory *segment;

static FGDataAllocator *fg_map_allocator;
static FGDataMap *fg_data_map;
static IntAllocator *int_allocator;
static ShmemIntSet *intnw_pids;
static named_upgradable_mutex *shmem_lock;

static bool creator = false;

#include <map>
#include <boost/thread.hpp>
#include <boost/thread/locks.hpp>
#include <boost/thread/shared_mutex.hpp>

struct proc_sock_info {
    pid_t pid;     // remote process id
    int remote_fd; // socket fd as the remote process knows it
    int local_fd;  // valid in this process; created by ancil_recv_fd

    bool operator<(const struct proc_sock_info& other) const {
        return (pid < other.pid ||
                (pid == other.pid && remote_fd < other.remote_fd));
    }
    bool operator==(const struct proc_sock_info& other) const {
        return (pid == other.pid &&
                remote_fd == other.remote_fd);
    }
    proc_sock_info(pid_t pid_, int remote_fd_, int local_fd_)
        : pid(pid_), remote_fd(remote_fd_), local_fd(local_fd_) {}
};
typedef std::map<struct in_addr, std::set<struct proc_sock_info>, 
                 in_addr_less> CSocketMap;

// contains process-unique FDs for all CSocket FDs in all intnw apps,
// separated by local interface
static CSocketMap *all_intnw_csockets;
static boost::shared_mutex *proc_local_lock;

struct fd_sharing_packet {
    struct in_addr ip_addr;
    pid_t pid;
    int remote_fd;
    char remove_fd; // 1 iff recipient should remove this remote_fd.
};


void add_or_remove_csocket(struct in_addr ip_addr, 
                           pid_t pid, int remote_fd, int local_fd, 
                           bool remove_fd)
{
    boost::upgrade_lock<boost::shared_mutex> lock(*proc_local_lock);
    if (all_intnw_csockets->count(ip_addr) == 0) {
        // must have been removed by scout; ignore
        dbgprintf("Cannot %s socket %d %s sockset; iface %s unknown\n",
                  remove_fd ? "remove" : "add", local_fd, 
                  remove_fd ? "from"   : "to",
                  inet_ntoa(ip_addr));
        return;
    }
    
    boost::unique_lock<boost::shared_mutex> writelock(boost::move(lock));
    proc_sock_info info(pid, remote_fd, local_fd);
    if (remove_fd) {
        dbgprintf("removing (PID: %d remote_fd: %d)"
                  " info from iface %s\n", pid, remote_fd,
                  inet_ntoa(ip_addr));
        (*all_intnw_csockets)[ip_addr].erase(info);
    } else {
        dbgprintf("adding (PID: %d remote_fd: %d local_fd: %d) "
                  "info to iface %s\n", 
                  pid, remote_fd, local_fd,
                  inet_ntoa(ip_addr));
        (*all_intnw_csockets)[ip_addr].insert(info);
    }
}

// to be followed by a PID
#define INTNW_FD_SHARING_SOCKET_FMT "IntNWFDSharingSocket_%d"

struct FDSharingThread {
    int sock;
    
    FDSharingThread() {
        sock = socket(AF_UNIX, SOCK_DGRAM, 0);
        if (sock < 0) {
            perror("FDSharingThread: socket");
        } else {
            struct sockaddr_un addr;
            memset(&addr, 0, sizeof(addr));
            addr.sun_family = AF_UNIX;
            snprintf(&addr.sun_path[1], UNIX_PATH_MAX - 2,
                     INTNW_FD_SHARING_SOCKET_FMT, getpid());
            int rc = bind(sock, (struct sockaddr*)&addr,
                          sizeof(addr));
            if (rc < 0) {
                perror("FDSharingThread: bind");
                dbgprintf("Failed to bind FDSharingThread socket\n");
                close(sock);
                sock = -1;
            }
        }
    }

    void operator()() {
        if (sock < 0) {
            return;
        }

        char name[MAX_NAME_LEN+1];
        strncpy(name, "FDSharingThread", MAX_NAME_LEN);
        set_thread_name(name);
        
        while (1) {
            struct sockaddr_un addr;
            socklen_t addrlen = sizeof(addr);
            struct fd_sharing_packet packet;
            int new_fd = -1;
            int rc = recvfrom(sock, &packet, sizeof(packet), 0, 
                              (struct sockaddr *)&addr, &addrlen);
            if (rc != sizeof(packet)) {
                if (rc < 0) {
                    perror("FDSharingThread: recvfrom");
                    break;
                } else {
                    dbgprintf("Error: FDSharingThread expected "
                              "%zu bytes, received %d\n", sizeof(packet), rc);
                    break;
                }
            }

            if (!packet.remove_fd) {
                rc = ancil_recv_fd(sock, &new_fd);
                if (rc != 0) {
                    perror("FDSharingThread: ancil_recv_fd");
                    break;
                }
            }

            dbgprintf("Received shared socket from PID %d: "
                      "iface %s remote_fd %d local_fd %d remove? %s\n",
                      packet.pid, inet_ntoa(packet.ip_addr), 
                      packet.remote_fd, new_fd, 
                      packet.remove_fd ? "yes" : "no");

            add_or_remove_csocket(packet.ip_addr, 
                                  packet.pid, packet.remote_fd, new_fd,
                                  packet.remove_fd);
        }
        close(sock);
    }
};

static FDSharingThread fd_sharing_thread_data;
static boost::thread *fd_sharing_thread;

static int send_local_csocket_fd(struct in_addr ip_addr, pid_t target, 
                                 int local_fd, bool remove_fd)
{
    struct fd_sharing_packet packet;
    packet.ip_addr = ip_addr;
    packet.pid = getpid();
    packet.remote_fd = local_fd;
    packet.remove_fd = remove_fd ? 1 : 0;

    // send packet
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(&addr.sun_path[1], UNIX_PATH_MAX - 2,
             INTNW_FD_SHARING_SOCKET_FMT, target);

    dbgprintf("Sending socket fd %d to PID %d remove? %s\n",
              local_fd, target, remove_fd ? "yes" : "no");
    boost::unique_lock<boost::shared_mutex> lock(*proc_local_lock);
    int rc = sendto(fd_sharing_thread_data.sock, &packet, sizeof(packet), 0,
                    (struct sockaddr *)&addr, sizeof(addr));
    if (rc == sizeof(packet)) {
        if (!remove_fd) {
            rc = ancil_send_fd_to(fd_sharing_thread_data.sock, local_fd,
                                  (struct sockaddr *)&addr, sizeof(addr));
            if (rc != 0) {
                perror("ancil_send_fd_to");
                return rc;
            }
        }
        return 0;
    } else if (rc < 0) {
        return rc;
    } else {
        dbgprintf("WARNING: Sent local csocket packet size %zu, "
                  "only %d bytes sent\n", sizeof(packet), rc);
        return -1;
    }
}

static void add_proc(pid_t pid)
{
    scoped_lock<named_upgradable_mutex> lock(*shmem_lock);
    intnw_pids->insert(pid);
}

static void remove_proc(pid_t pid)
{
    {
        boost::unique_lock<boost::shared_mutex> local_lock(*proc_local_lock);
        for (CSocketMap::iterator it = all_intnw_csockets->begin(); 
             it != all_intnw_csockets->end(); it++) {
            std::set<struct proc_sock_info>& s = it->second;
            for (std::set<struct proc_sock_info>::iterator victim = s.begin();
                 victim != s.end(); ) {
                if (victim->pid == pid && pid != getpid()) {
                    close(victim->local_fd);
                    s.erase(victim++);
                } else { 
                    ++victim;
                }
            }
        }
    }
    scoped_lock<named_upgradable_mutex> lock(*shmem_lock);
    intnw_pids->erase(pid);
}

void ipc_shmem_init(bool create)
{
    // only one process should pass true here (for intnw, the scout)
    creator = create;

    // construct shared memory segment and data structures
    if (creator) {
        shared_memory_object::remove(INTNW_SHMEM_NAME);
        named_upgradable_mutex::remove(INTNW_SHMEM_MUTEX_NAME);

        segment = new managed_shared_memory(create_only, 
                                            INTNW_SHMEM_NAME, 
                                            INTNW_SHMEM_SIZE);
        shmem_lock = new named_upgradable_mutex(create_only,
                                                INTNW_SHMEM_MUTEX_NAME);
    } else {
        segment = new managed_shared_memory(open_only, INTNW_SHMEM_NAME);
        shmem_lock = new named_upgradable_mutex(open_only, 
                                                INTNW_SHMEM_MUTEX_NAME);
    }

    fg_map_allocator = new FGDataAllocator(segment->get_segment_manager());
    fg_data_map = segment->find_or_construct<FGDataMap>(INTNW_SHMEM_MAP_NAME)(
        in_addr_less(), *fg_map_allocator
        ); // never constructs; scout is first
    
    int_allocator = new IntAllocator(segment->get_segment_manager());
    intnw_pids = segment->find_or_construct<ShmemIntSet>(INTNW_SHMEM_PID_SET_NAME)(
        std::less<int>(), *int_allocator
        ); // never constructs; scout is first

    proc_local_lock = new boost::shared_mutex;
    all_intnw_csockets = new CSocketMap;
    add_proc(getpid());
    fd_sharing_thread = new boost::thread(boost::ref(fd_sharing_thread_data));

    // initialize the all_intnw_csockets map with the currently available
    //  network interfaces that the scout has added to the fg_data_map
    sharable_lock<named_upgradable_mutex> read_lock(*shmem_lock);
    boost::unique_lock<boost::shared_mutex> local_lock(*proc_local_lock);
    for (FGDataMap::iterator it = fg_data_map->begin(); 
         it != fg_data_map->end(); it++) {
        struct in_addr ip_addr = it->first;
        if (all_intnw_csockets->count(ip_addr) == 0) {
            // inserts empty set
            (*all_intnw_csockets)[ip_addr] = CSocketMap::mapped_type();
        }
    }
}

void ipc_shmem_deinit()
{
    shutdown(fd_sharing_thread_data.sock, SHUT_RDWR);
    fd_sharing_thread->join();
    delete fd_sharing_thread;
    
    remove_proc(getpid());
    delete all_intnw_csockets;
    delete proc_local_lock;
    // scout takes care of deallocating shared memory objects when it exits

    // clean up process-local data structures
    delete shmem_lock;
    if (creator) {
        segment->destroy<FGDataMap>(INTNW_SHMEM_MAP_NAME);
        segment->destroy<ShmemIntSet>(INTNW_SHMEM_PID_SET_NAME);
    }
    delete int_allocator;
    delete fg_map_allocator;
    delete segment;

    if (creator) {
        shared_memory_object::remove(INTNW_SHMEM_NAME);
        named_upgradable_mutex::remove(INTNW_SHMEM_MUTEX_NAME);
    }
}

static FGDataPtr map_lookup(struct in_addr ip_addr)
{
    sharable_lock<named_upgradable_mutex> read_lock(*shmem_lock);
    if (fg_data_map->find(ip_addr) != fg_data_map->end()) {
        FGDataPtr fg_data(fg_data_map->at(ip_addr));
        return fg_data;
    }

    return FGDataPtr();
}

gint ipc_last_fg_tv_sec(struct in_addr ip_addr)
{
    FGDataPtr fg_data = map_lookup(ip_addr);
    if (fg_data) {
        return g_atomic_int_get(&fg_data->last_fg_tv_sec);
    } else {
        return 0;
    }
}

/*
gint ipc_fg_sender_count(struct in_addr ip_addr)
{
    FGDataPtr fg_data = map_lookup(ip_addr);
    if (fg_data) {
        return g_atomic_int_get(&fg_data->num_fg_senders);
    } else {
        return 0;
    }
}
*/

void ipc_update_fg_timestamp(struct in_addr ip_addr)
{
    struct timeval now;
    TIME(now);
    ipc_set_last_fg_tv_sec(ip_addr, now.tv_sec);
}

void ipc_set_last_fg_tv_sec(struct in_addr ip_addr, gint secs)
{
    FGDataPtr fg_data = map_lookup(ip_addr);
    if (fg_data) {
        g_atomic_int_set(&fg_data->last_fg_tv_sec, secs);
    }
}

/*
void ipc_increment_fg_senders(struct in_addr ip_addr)
{
    FGDataPtr fg_data = map_lookup(ip_addr);
    if (fg_data) {
        boost::upgrade_lock<boost::shared_mutex> lock(*proc_local_lock);
        if (proc_local_sending_fg_map.count(ip_addr) == 0 ||
            proc_local_sending_fg_map[ip_addr] == false) {
            // only increment once per process (until decrement)
            boost::unique_lock<boost::shared_mutex> wrlock(boost::move(lock));
            proc_local_sending_fg_map[ip_addr] = true;

            g_atomic_int_inc(&fg_data->num_fg_senders);
        }
    }
}

void ipc_decrement_fg_senders(struct in_addr ip_addr)
{
    FGDataPtr fg_data = map_lookup(ip_addr);
    if (fg_data) {
        boost::upgrade_lock<boost::shared_mutex> lock(*proc_local_lock);
        if (proc_local_sending_fg_map.count(ip_addr) > 0 &&
            proc_local_sending_fg_map[ip_addr] == true) {
            // only decrement once per process (until increment)
            boost::unique_lock<boost::shared_mutex> wrlock(boost::move(lock));
            proc_local_sending_fg_map[ip_addr] = false;

            (void)g_atomic_int_dec_and_test(&fg_data->num_fg_senders);
        }
    }
}

// call when there are now no FG IROBs in flight
void ipc_decrement_all_fg_senders()
{
    std::vector<struct in_addr> ifaces;
    {
        boost::shared_lock<boost::shared_mutex> lock(*proc_local_lock);
        for (std::map<struct in_addr, bool>::iterator it 
                 = proc_local_sending_fg_map.begin();
             it != proc_local_sending_fg_map.end(); it++) {
            ifaces.push_back(it->first);
        }
    }

    for (size_t i = 0; i < ifaces.size(); ++i) {
        ipc_decrement_fg_senders(ifaces[i]);
    }
}
*/

bool ipc_add_iface(struct in_addr ip_addr)
{
    upgradable_lock<named_upgradable_mutex> lock(*shmem_lock);
    if (!map_lookup(ip_addr)) {
        FGDataPtr new_data = make_managed_shared_ptr(
            segment->construct<struct fg_iface_data>(anonymous_instance)(),
            *segment
            );
        new_data->last_fg_tv_sec = 0;
        //new_data->num_fg_senders = 0;

        // upgrade to exclusive
        scoped_lock<named_upgradable_mutex> writelock(move(lock));
        fg_data_map->insert(make_pair(ip_addr, new_data));
        writelock.unlock();

        boost::unique_lock<boost::shared_mutex> local_lock(*proc_local_lock);
        if (all_intnw_csockets->count(ip_addr) == 0) {
            // inserts empty set
            (*all_intnw_csockets)[ip_addr] = CSocketMap::mapped_type();
        }
        /*
        if (proc_local_sending_fg_map.count(ip_addr) == 0) {
            proc_local_sending_fg_map[ip_addr] = false;
        }
        */
        dbgprintf("Added iface %s for tracking socket buffers\n",
                  inet_ntoa(ip_addr));
        return true;
    } else {
        dbgprintf("iface %s already added for tracking socket buffers\n",
                  inet_ntoa(ip_addr));
        return false;
    }
}

bool ipc_remove_iface(struct in_addr ip_addr)
{
    upgradable_lock<named_upgradable_mutex> lock(*shmem_lock);
    if (map_lookup(ip_addr)) {
        // upgrade to exclusive
        scoped_lock<named_upgradable_mutex> writelock(move(lock));
        fg_data_map->erase(ip_addr);
        writelock.unlock();

        boost::unique_lock<boost::shared_mutex> local_lock(*proc_local_lock);
        if (all_intnw_csockets->count(ip_addr) > 0) {
            // clean up all local (dup'd) file descriptors
            std::set<proc_sock_info>& sockinfo = (*all_intnw_csockets)[ip_addr];
            for (std::set<proc_sock_info>::iterator it = sockinfo.begin();
                 it != sockinfo.end(); it++) {
                if (it->pid != getpid()) {
                    // only close the ones that are dups from other procs.
                    // my own csocket fds will get closed by their
                    // sender threads.
                    close(it->local_fd);
                }
            }
            all_intnw_csockets->erase(ip_addr);
        }
        /*
        if (proc_local_sending_fg_map.count(ip_addr) > 0) {
            proc_local_sending_fg_map.erase(ip_addr);
        }
        */

        dbgprintf("Removed iface %s from tracking socket buffers\n",
                  inet_ntoa(ip_addr));
        return true;
    } else {
        dbgprintf("iface %s already removed from tracking socket buffers\n",
                  inet_ntoa(ip_addr));
        return false;
    }
}

bool send_csocket_to_all_pids(struct in_addr ip_addr, int local_fd, 
                              bool remove_fd)
{
    std::vector<pid_t> failed_procs;
    bool rc;

    {
        sharable_lock<named_upgradable_mutex> lock(*shmem_lock);
        if (map_lookup(ip_addr)) {
            std::set<pid_t> target_procs(intnw_pids->begin(), intnw_pids->end());
            lock.unlock();
            
            dbgprintf("Sending local socket fd %d to %d pids\n",
                      local_fd, target_procs.size());
            for (std::set<pid_t>::iterator it = target_procs.begin();
                 it != target_procs.end(); it++) {
                pid_t pid = *it;
                if (pid != getpid()) {
                    int rc = send_local_csocket_fd(ip_addr, pid, local_fd, 
                                                   remove_fd);
                    if (rc != 0) {
                        failed_procs.push_back(pid);
                    }
                }
            }
            rc = true;
        } else {
            rc = false;
        }
    }
    for (size_t i = 0; i < failed_procs.size(); ++i) {
        remove_proc(failed_procs[i]);
    }
    return rc;
}

bool ipc_add_csocket(struct in_addr ip_addr, int local_fd)
{
    add_or_remove_csocket(ip_addr, getpid(), local_fd, local_fd, false);
    return send_csocket_to_all_pids(ip_addr, local_fd, false);
}

bool ipc_remove_csocket(struct in_addr ip_addr, int local_fd)
{
    add_or_remove_csocket(ip_addr, getpid(), local_fd, local_fd, true);
    return send_csocket_to_all_pids(ip_addr, local_fd, true);
}

size_t ipc_total_bytes_inflight(struct in_addr ip_addr)
{
    size_t bytes = 0;
    size_t num_sockets = 0;
    boost::shared_lock<boost::shared_mutex> lock(*proc_local_lock);
    if (map_lookup(ip_addr)) {
        std::set<struct proc_sock_info>& sockinfo = (*all_intnw_csockets)[ip_addr];
        num_sockets = sockinfo.size();
        for (std::set<struct proc_sock_info>::iterator it = sockinfo.begin();
             it != sockinfo.end(); it++) {
            int unsent_bytes = get_unsent_bytes(it->local_fd);
            if (unsent_bytes < 0) {
                dbgprintf("Error checking buffer usage for socket %d\n",
                          it->local_fd);
            } else {
                dbgprintf("Socket %d has %zu bytes in buffer\n",
                          it->local_fd, unsent_bytes);
                bytes += unsent_bytes;
            }
        }

        if (sockinfo.empty()) {
            dbgprintf("iface %s has no connected sockets\n",
                      inet_ntoa(ip_addr));
        }
    } else {
        dbgprintf("total_bytes_inflight: unknown iface %s ; returning 0\n",
                  inet_ntoa(ip_addr));
    }

    dbgprintf("total_bytes_inflight: counted %zu bytes in %zu sockets\n", 
              bytes, num_sockets);

    return bytes;
}

#endif