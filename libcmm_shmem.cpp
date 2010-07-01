#include "libcmm_shmem.h"
#include "timeops.h"

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
using std::make_pair;

static managed_shared_memory *segment;

static FGDataAllocator *fg_map_allocator;
static FGDataMap *fg_data_map;
static named_upgradable_mutex *fg_data_map_lock;

static bool creator = false;

#include <map>
#include <boost/thread/locks.hpp>
#include <boost/thread/shared_mutex.hpp>

typedef std::map<struct in_addr, bool, in_addr_less> SendingFGMap;
static SendingFGMap proc_local_sending_fg_map;
static boost::shared_mutex proc_local_fg_map_lock;

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
        fg_data_map_lock = new named_upgradable_mutex(create_only,
                                                      INTNW_SHMEM_MUTEX_NAME);
    } else {
        segment = new managed_shared_memory(open_only, INTNW_SHMEM_NAME);
        fg_data_map_lock = new named_upgradable_mutex(open_only, 
                                                      INTNW_SHMEM_MUTEX_NAME);
    }

    fg_map_allocator = new FGDataAllocator(segment->get_segment_manager());
    fg_data_map = segment->find_or_construct<FGDataMap>(INTNW_SHMEM_MAP_NAME)(in_addr_less(), *fg_map_allocator); // never constructs; scout is first
}

void ipc_shmem_deinit()
{
    // scout takes care of deallocating shared memory objects when it exits

    // clean up process-local data structures
    delete fg_data_map_lock;
    if (creator) {
        segment->destroy<FGDataMap>(INTNW_SHMEM_MAP_NAME);
    }
    delete fg_map_allocator;
    delete segment;

    if (creator) {
        shared_memory_object::remove(INTNW_SHMEM_NAME);
        named_upgradable_mutex::remove(INTNW_SHMEM_MUTEX_NAME);
    }
}

static FGDataPtr map_lookup(struct in_addr ip_addr)
{
    sharable_lock<named_upgradable_mutex> read_lock(*fg_data_map_lock);
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

gint ipc_fg_sender_count(struct in_addr ip_addr)
{
    FGDataPtr fg_data = map_lookup(ip_addr);
    if (fg_data) {
        return g_atomic_int_get(&fg_data->num_fg_senders);
    } else {
        return 0;
    }
}

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


void ipc_increment_fg_senders(struct in_addr ip_addr)
{
    FGDataPtr fg_data = map_lookup(ip_addr);
    if (fg_data) {
        boost::upgrade_lock<boost::shared_mutex> lock(proc_local_fg_map_lock);
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
        boost::upgrade_lock<boost::shared_mutex> lock(proc_local_fg_map_lock);
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
        boost::shared_lock<boost::shared_mutex> lock(proc_local_fg_map_lock);
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

bool ipc_add_iface(struct in_addr ip_addr)
{
    upgradable_lock<named_upgradable_mutex> lock(*fg_data_map_lock);
    if (!map_lookup(ip_addr)) {
        FGDataPtr new_data = make_managed_shared_ptr(
            segment->construct<struct fg_iface_data>(anonymous_instance)(),
            *segment
            );
        new_data->last_fg_tv_sec = 0;
        new_data->num_fg_senders = 0;

        // upgrade to exclusive
        scoped_lock<named_upgradable_mutex> writelock(move(lock));
        fg_data_map->insert(make_pair(ip_addr, new_data));
        //writelock.release();

        boost::unique_lock<boost::shared_mutex> local_lock(proc_local_fg_map_lock);
        if (proc_local_sending_fg_map.count(ip_addr) == 0) {
            proc_local_sending_fg_map[ip_addr] = false;
        }

        return true;
    } else {
        return false;
    }
}

bool ipc_remove_iface(struct in_addr ip_addr)
{
    upgradable_lock<named_upgradable_mutex> lock(*fg_data_map_lock);
    if (map_lookup(ip_addr)) {
        // upgrade to exclusive
        scoped_lock<named_upgradable_mutex> writelock(move(lock));
        fg_data_map->erase(ip_addr);
        //writelock.release();

        boost::unique_lock<boost::shared_mutex> local_lock(proc_local_fg_map_lock);
        if (proc_local_sending_fg_map.count(ip_addr) > 0) {
            proc_local_sending_fg_map.erase(ip_addr);
        }
        return true;
    } else {
        return false;
    }
}


#endif
