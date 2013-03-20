#include "libcmm_shmem.h"
#include "timeops.h"
#include "common.h"
#include "pthread_util.h"

#ifdef ANDROID
/* TODO: use Android's /dev/ashmem and IBinder interfaces
 *       to do the shared memory stuff. 
 */
#else
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
#include <sstream>
using std::make_pair;
using std::ostringstream;

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
typedef std::map<struct iface_pair, // local/remote iface pair
                 std::set<struct proc_sock_info> > CSocketMap;

// contains process-unique FDs for all CSocket FDs in all intnw apps,
// separated by local interface
static CSocketMap *all_intnw_csockets;
static boost::shared_mutex *proc_local_lock;

struct fd_sharing_packet {
    struct iface_pair ifaces;//struct in_addr ip_addr;
    pid_t pid;
    int remote_fd;
    char remove_fd; // 1 iff recipient should remove this remote_fd.
};

void add_or_remove_csocket(struct iface_pair ifaces, //struct in_addr ip_addr, 
                           pid_t pid, int remote_fd, int local_fd, 
                           bool remove_fd)
{
    bool dropped_lock = false;
    boost::upgrade_lock<boost::shared_mutex> lock(*proc_local_lock);
    if (all_intnw_csockets->count(ifaces) == 0) {
        ostringstream s;
        s << "add_or_remove_csocket: adding missing iface pair ";
        ifaces.print(s);
        s << "\n";
        dbgprintf("%s", s.str().c_str());

        lock.unlock();
        dropped_lock = true;
        ipc_add_iface_pair(ifaces);

        // must have been removed by scout; ignore
    }
    
    boost::unique_lock<boost::shared_mutex> writelock;
    if (dropped_lock) {
        // grab the lock anew
        writelock = boost::unique_lock<boost::shared_mutex>(*proc_local_lock);
    } else {
        // upgrade from reader
        writelock = boost::unique_lock<boost::shared_mutex>(boost::move(lock));
    }
    proc_sock_info info(pid, remote_fd, local_fd);
    if (remove_fd) {
        ostringstream s;
        s << "removing (PID: " << pid << " remote_fd: " << remote_fd
          << ") info from iface pair";
        ifaces.print(s);
        dbgprintf("%s\n", s.str().c_str());
        
        if (pid != getpid() &&
            (*all_intnw_csockets)[ifaces].count(info) > 0) {
            local_fd = (*all_intnw_csockets)[ifaces].find(info)->local_fd;
            dbgprintf("Closing local (dup'd) socket %d\n", local_fd);
            close(local_fd);
        }
        (*all_intnw_csockets)[ifaces].erase(info);
    } else {
        ostringstream s;
        s << "adding (PID: " << pid 
          << " remote_fd: " << remote_fd << " local_fd: " << local_fd 
          << ") info to iface pair ";
        ifaces.print(s);
        dbgprintf("%s\n", s.str().c_str());
        (*all_intnw_csockets)[ifaces].insert(info);
    }
}

// to be followed by a PID
#define INTNW_FD_SHARING_SOCKET_FMT "IntNWFDSharingSocket_%d"

static int send_fd_sharing_packet(const struct fd_sharing_packet& packet, pid_t target);


static void add_proc(pid_t pid);

struct FDSharingThread {
private:
    bool running;
    pthread_mutex_t running_mutex;
    pid_t pid;

public:
    int sock;
    
    FDSharingThread() {
        pthread_mutex_init(&running_mutex, NULL);
        running = true;
        
        sock = -1;
        pid = getpid();
        bind_socket();
    }

    void bind_socket() {
        if (sock != -1) {
            close(sock);
        }
        dbgprintf("Binding fd sharing socket to unix path: "
                  INTNW_FD_SHARING_SOCKET_FMT "\n", getpid());
        
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


    bool is_running() {
        PthreadScopedLock lock(&running_mutex);
        return running;
    }

    void interrupt() {
        PthreadScopedLock lock(&running_mutex);
        running = false;
        
        struct fd_sharing_packet packet;
        memset(&packet, 0, sizeof(packet));
        packet.pid = -1;
        send_fd_sharing_packet(packet, getpid());
    }

    void operator()() {
        if (sock < 0) {
            return;
        }

        char name[MAX_NAME_LEN+1];
        strncpy(name, "FDSharingThread", MAX_NAME_LEN);
        set_thread_name(name);
        
        while (is_running()) {
            struct sockaddr_un addr;
            socklen_t addrlen = sizeof(addr);
            struct fd_sharing_packet packet;
            int recvd_fd = -1;

            struct timeval timeout = {1, 0};
            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(sock, &fds);
            int rc = select(sock + 1, &fds, NULL, NULL, &timeout);
            if (rc == 0) {
                if (pid != getpid()) {
                    // pid has changed; this means we're the child of a fork()
                    // XXX: hack to handle fork().  The Right(er) Way to do this
                    // XXX: would be to do away with the UNIX datagram sockets
                    // XXX: and just handle this through the connection
                    // XXX: to the scout.
                    pid = getpid();
                    bind_socket();
                    add_proc(pid);
                    dbgprintf("Re-initialized FDSharingThread with new pid, %d\n", pid);
                    dbgprintf("WARNING: this means app has forked after creating a multisocket.\n"
                              "It should never do this; we don't handle it.\n");
                }
                continue;
            } else if (rc < 0) {
                dbgprintf("Error: select failed: %s\n", strerror(errno));
                break;
            }
            rc = recvfrom(sock, &packet, sizeof(packet), 0, 
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

            if (packet.pid == -1) {
                dbgprintf("FDSharingThread received shutdown signal; exiting.\n");
                break;
            }

            if (!packet.remove_fd) {
                rc = ancil_recv_fd(sock, &recvd_fd);
                if (rc != 0) {
                    perror("FDSharingThread: ancil_recv_fd");
                    break;
                }
            }

            ostringstream s;
            s << "Received shared socket from PID " << packet.pid 
              << ": iface pair ";
            packet.ifaces.print(s);
            s << " remote_fd "<< packet.remote_fd
              << " local_fd " << recvd_fd
              << " remove? " << (packet.remove_fd ? "yes" : "no");
            dbgprintf("%s\n", s.str().c_str());

            add_or_remove_csocket(packet.ifaces, 
                                  packet.pid, packet.remote_fd, recvd_fd,
                                  packet.remove_fd);
        }
        dbgprintf("FDSharingThread exiting.\n");
        close(sock);
    }
};

static FDSharingThread *fd_sharing_thread_data = NULL;
static boost::thread *fd_sharing_thread = NULL;

static int send_local_csocket_fd(struct iface_pair ifaces, //struct in_addr ip_addr, 
                                 pid_t target, 
                                 int local_fd, bool remove_fd)
{
    struct fd_sharing_packet packet;
    memset(&packet, 0, sizeof(packet));
    packet.ifaces = ifaces;
    packet.pid = getpid();
    packet.remote_fd = local_fd;
    packet.remove_fd = remove_fd ? 1 : 0;

    return send_fd_sharing_packet(packet, target);
}

static int send_fd_sharing_packet(const struct fd_sharing_packet& packet, pid_t target)
{
    // send packet
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(&addr.sun_path[1], UNIX_PATH_MAX - 2,
             INTNW_FD_SHARING_SOCKET_FMT, target);

    dbgprintf("Sending socket fd %d to PID %d remove? %s\n",
              packet.remote_fd, target, packet.remove_fd ? "yes" : "no");
    boost::unique_lock<boost::shared_mutex> lock(*proc_local_lock);
    int rc = sendto(fd_sharing_thread_data->sock, &packet, sizeof(packet), 0,
                    (struct sockaddr *)&addr, sizeof(addr));
    if (rc == sizeof(packet)) {
        if (!packet.remove_fd) {
            rc = ancil_send_fd_to(fd_sharing_thread_data->sock, packet.remote_fd,
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
#endif /* !ANDROID */

void ipc_shmem_init(bool create)
{
#ifdef ANDROID
    /* TODO: use Android's /dev/ashmem and IBinder interfaces
     *       to do the shared memory stuff. 
     */
#else
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
        std::less<struct iface_pair>(), *fg_map_allocator
        ); // never constructs; scout is first
    
    int_allocator = new IntAllocator(segment->get_segment_manager());
    intnw_pids = segment->find_or_construct<ShmemIntSet>(INTNW_SHMEM_PID_SET_NAME)(
        std::less<int>(), *int_allocator
        ); // never constructs; scout is first

    proc_local_lock = new boost::shared_mutex;
    all_intnw_csockets = new CSocketMap;
    add_proc(getpid());
    fd_sharing_thread_data = new FDSharingThread;
    fd_sharing_thread = new boost::thread(boost::ref(*fd_sharing_thread_data));

    // initialize the all_intnw_csockets map with the currently available
    //  network interfaces that the scout has added to the fg_data_map
    sharable_lock<named_upgradable_mutex> read_lock(*shmem_lock);
    boost::unique_lock<boost::shared_mutex> local_lock(*proc_local_lock);
    for (FGDataMap::iterator it = fg_data_map->begin(); 
         it != fg_data_map->end(); it++) {
        //struct in_addr ip_addr = it->first;
        struct iface_pair ifaces = it->first;
        if (all_intnw_csockets->count(ifaces) == 0) {
            // inserts empty set
            (*all_intnw_csockets)[ifaces] = CSocketMap::mapped_type();
        }
    }
#endif /* !ANDROID */
}

void ipc_shmem_deinit()
{
#ifdef ANDROID
    /* TODO: use Android's /dev/ashmem and IBinder interfaces
     *       to do the shared memory stuff. 
     */
#else
    if (!fd_sharing_thread_data) {
        // ipc_shmem_init was never called.
        return;
    }

    fd_sharing_thread_data->interrupt();
    fd_sharing_thread->join();
    delete fd_sharing_thread;
    delete fd_sharing_thread_data;
    
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
#endif /* !ANDROID */
}

#ifdef ANDROID
/* TODO: use Android's /dev/ashmem and IBinder interfaces
 *       to do the shared memory stuff. 
 */
#else
static FGDataPtr map_lookup(struct iface_pair ifaces, bool grab_lock = true)
{
    sharable_lock<named_upgradable_mutex> read_lock;
    if (grab_lock) {
        // should transfer ownership with assignment
        read_lock = sharable_lock<named_upgradable_mutex>(*shmem_lock);
    }
    if (fg_data_map->find(ifaces) != fg_data_map->end()) {
        FGDataPtr fg_data(fg_data_map->at(ifaces));
        return fg_data;
    }

    return FGDataPtr();
}
#endif /* !ANDROID */

#ifndef BUILDING_SCOUT
gint ipc_last_fg_tv_sec(CSocketPtr csock)//struct in_addr ip_addr)
{
#ifdef ANDROID
    /* TODO: use Android's /dev/ashmem and IBinder interfaces
     *       to do the shared memory stuff. 
     */
    // for now, just mimic the old behavior.
    return csock->get_last_fg().tv_sec;
#else
    TimeFunctionBody timer("SHMEM_TIMING: ipc_last_fg_tv_sec");
    struct iface_pair ifaces(csock->local_iface.ip_addr,
                             csock->remote_iface.ip_addr);
    FGDataPtr fg_data = map_lookup(ifaces);
    if (fg_data) {
        return g_atomic_int_get(&fg_data->last_fg_tv_sec);
    } else {
        return 0;
    }
#endif /* !ANDROID */
}

/*
gint ipc_fg_sender_count(struct in_addr ip_addr)
{
TimeFunctionBody timer("SHMEM_TIMING: ipc_fg_sender_count");
    FGDataPtr fg_data = map_lookup(ip_addr);
    if (fg_data) {
        return g_atomic_int_get(&fg_data->num_fg_senders);
    } else {
        return 0;
    }
}
*/

void ipc_update_fg_timestamp(struct iface_pair ifaces) //struct in_addr ip_addr)
{
#ifdef ANDROID
    /* TODO: use Android's /dev/ashmem and IBinder interfaces
     *       to do the shared memory stuff. 
     */
#else
    TimeFunctionBody timer("SHMEM_TIMING: ipc_update_fg_timestamp");
    struct timeval now;
    TIME(now);
    ipc_set_last_fg_tv_sec(ifaces, now.tv_sec);
#endif
}

void ipc_set_last_fg_tv_sec(struct iface_pair ifaces, //struct in_addr ip_addr, 
                            gint secs)
{
#ifdef ANDROID
    /* TODO: use Android's /dev/ashmem and IBinder interfaces
     *       to do the shared memory stuff. 
     */
#else
    TimeFunctionBody timer("SHMEM_TIMING: ipc_set_last_fg_tv_sec");

//     struct iface_pair ifaces(csock->local_iface.ip_addr,
//                              csock->remote_iface.ip_addr);
    FGDataPtr fg_data = map_lookup(ifaces);
    if (fg_data) {
        g_atomic_int_set(&fg_data->last_fg_tv_sec, secs);
    }
#endif
}
#endif // BUILDING_SCOUT

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
TimeFunctionBody timer("SHMEM_TIMING: ipc_decrement_fg_senders");
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
TimeFunctionBody timer("SHMEM_TIMING: ipc_decrement_all_fg_senders");
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

bool ipc_add_iface_pair(struct iface_pair ifaces)
{
#ifdef ANDROID
    /* TODO: use Android's /dev/ashmem and IBinder interfaces
     *       to do the shared memory stuff. 
     */
    return true;
#else
    TimeFunctionBody timer("SHMEM_TIMING: ipc_add_iface");
    upgradable_lock<named_upgradable_mutex> lock(*shmem_lock);
    if (!map_lookup(ifaces, false)) {
        FGDataPtr new_data = make_managed_shared_ptr(
            segment->construct<struct fg_iface_data>(anonymous_instance)(),
            *segment
            );
        new_data->last_fg_tv_sec = 0;
        //new_data->num_fg_senders = 0;

        // upgrade to exclusive
        scoped_lock<named_upgradable_mutex> writelock(move(lock));
        fg_data_map->insert(make_pair(ifaces, new_data));
        writelock.unlock();

        boost::unique_lock<boost::shared_mutex> local_lock(*proc_local_lock);
        if (all_intnw_csockets->count(ifaces) == 0) {
            // inserts empty set
            (*all_intnw_csockets)[ifaces] = CSocketMap::mapped_type();
        }
        /*
        if (proc_local_sending_fg_map.count(ifaces) == 0) {
            proc_local_sending_fg_map[ifaces] = false;
        }
        */
        ostringstream s;
        s << "Added iface pair ";
        ifaces.print(s);
        dbgprintf("%s for tracking socket buffers\n", s.str().c_str());
        return true;
    } else {
        ostringstream s;
        ifaces.print(s);
        dbgprintf("iface %s already added for tracking socket buffers\n",
                  s.str().c_str());
        return false;
    }
#endif
}

bool ipc_remove_iface_pair(struct iface_pair ifaces)
{
#ifdef ANDROID
    /* TODO: use Android's /dev/ashmem and IBinder interfaces
     *       to do the shared memory stuff. 
     */
    return true;
#else
    TimeFunctionBody timer("SHMEM_TIMING: ipc_remove_iface");
    upgradable_lock<named_upgradable_mutex> lock(*shmem_lock);
    if (map_lookup(ifaces, false)) {
        // upgrade to exclusive
        scoped_lock<named_upgradable_mutex> writelock(move(lock));
        fg_data_map->erase(ifaces);
        writelock.unlock();

        boost::unique_lock<boost::shared_mutex> local_lock(*proc_local_lock);
        if (all_intnw_csockets->count(ifaces) > 0) {
            // clean up all local (dup'd) file descriptors
            std::set<proc_sock_info>& sockinfo = (*all_intnw_csockets)[ifaces];
            for (std::set<proc_sock_info>::iterator it = sockinfo.begin();
                 it != sockinfo.end(); it++) {
                if (it->pid != getpid()) {
                    // only close the ones that are dups from other procs.
                    // my own csocket fds will get closed by their
                    // sender threads.
                    close(it->local_fd);
                }
            }
            all_intnw_csockets->erase(ifaces);
        }
        /*
        if (proc_local_sending_fg_map.count(ifaces) > 0) {
            proc_local_sending_fg_map.erase(ifaces);
        }
        */

        ostringstream s;
        ifaces.print(s);
        dbgprintf("Removed iface pair %s from tracking socket buffers\n",
                  s.str().c_str());
        return true;
    } else {
        ostringstream s;
        ifaces.print(s);
        dbgprintf("iface pair %s already removed from tracking socket buffers\n",
                  s.str().c_str());
        return false;
    }
#endif
}

#ifdef ANDROID
/* TODO: use Android's /dev/ashmem and IBinder interfaces
 *       to do the shared memory stuff. 
 */
#else
bool send_csocket_to_all_pids(struct iface_pair ifaces, int local_fd, 
                              bool remove_fd)
{
    std::vector<pid_t> failed_procs;
    bool ret;

    {
        sharable_lock<named_upgradable_mutex> lock(*shmem_lock);
        if (true) {//map_lookup(ifaces, false)) {
            //I don't actually care if it's in the lookup table; 
            // I'll only call this once per CSocket destruction,
            // and I need to send the update, even if the iface
            // pair has been removed by a scout update.
            std::set<pid_t> target_procs(intnw_pids->begin(), intnw_pids->end());
            lock.unlock();
            
            dbgprintf("Sending local socket fd %d to %d pids\n",
                      local_fd, target_procs.size());
            for (std::set<pid_t>::iterator it = target_procs.begin();
                 it != target_procs.end(); it++) {
                pid_t pid = *it;
                if (pid != getpid()) {
                    int rc = send_local_csocket_fd(ifaces, pid, local_fd, 
                                                   remove_fd);
                    if (rc != 0) {
                        failed_procs.push_back(pid);
                    }
                }
            }
            ret = true;
        } else {
            ret = false;
        }
    }
    for (size_t i = 0; i < failed_procs.size(); ++i) {
        remove_proc(failed_procs[i]);
    }
    return ret;
}
#endif /* !ANDROID */

#ifndef BUILDING_SCOUT
bool ipc_add_csocket(CSocketPtr csock, //struct in_addr ip_addr,
                     int local_fd)
{
#ifdef ANDROID
    /* TODO: use Android's /dev/ashmem and IBinder interfaces
     *       to do the shared memory stuff. 
     */
     
    return true;
#else
    TimeFunctionBody timer("SHMEM_TIMING: ipc_add_csocket");
    struct iface_pair ifaces(csock->local_iface.ip_addr,
                             csock->remote_iface.ip_addr);
    add_or_remove_csocket(ifaces, getpid(), local_fd, local_fd, false);
    return send_csocket_to_all_pids(ifaces, local_fd, false);
#endif
}

bool ipc_remove_csocket(struct iface_pair ifaces, // struct in_addr ip_addr,
                        int local_fd)
{
#ifdef ANDROID
    /* TODO: use Android's /dev/ashmem and IBinder interfaces
     *       to do the shared memory stuff. 
     */
    return true;
#else
    TimeFunctionBody timer("SHMEM_TIMING: ipc_remove_csocket");
    // struct iface_pair ifaces(csock->local_iface.ip_addr,
//                              csock->remote_iface.ip_addr);
    add_or_remove_csocket(ifaces, getpid(), local_fd, local_fd, true);
    return send_csocket_to_all_pids(ifaces, local_fd, true);
#endif
}

size_t ipc_total_bytes_inflight(CSocketPtr csock)//struct in_addr ip_addr)
{
#ifdef ANDROID
    /* TODO: use Android's /dev/ashmem and IBinder interfaces
     *       to do the shared memory stuff. 
     */
    return get_unsent_bytes(csock->osfd);
#else
    //TimeFunctionBody timer("SHMEM_TIMING: ipc_total_bytes_inflight");

    // for anticipatory scheduling, group the socket byte counting by
    //  the local/remote iface pair.
    //struct in_addr ip_addr = csock->bottleneck_iface().ip_addr;
    struct iface_pair ifaces(csock->local_iface.ip_addr,
                             csock->remote_iface.ip_addr);

    size_t bytes = 0;
    size_t num_sockets = 0;
    boost::shared_lock<boost::shared_mutex> lock(*proc_local_lock);
    // XXX: wrong lock order?  It's a shared lock, so it might not matter...
    if (map_lookup(ifaces) && all_intnw_csockets->count(ifaces) > 0) {
        std::set<struct proc_sock_info>& sockinfo = (*all_intnw_csockets)[ifaces];
        num_sockets = sockinfo.size();
        for (std::set<struct proc_sock_info>::iterator it = sockinfo.begin();
             it != sockinfo.end(); it++) {
            int unsent_bytes = get_unsent_bytes(it->local_fd);
            if (unsent_bytes < 0) {
                dbgprintf("Error checking buffer usage for socket %d\n",
                          it->local_fd);
            } else {
                /*dbgprintf("Socket %d has %zu bytes in buffer\n",
                 *          it->local_fd, unsent_bytes);*/
                bytes += unsent_bytes;
            }
        }
    }

    /*dbgprintf("total_bytes_inflight: counted %zu bytes in %zu sockets\n", 
     *          bytes, num_sockets);*/
    (void)num_sockets;

    return bytes;
#endif /* !ANDROID */
}
#endif // BUILDING_SCOUT
