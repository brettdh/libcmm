#include "libcmm_shmem.h"

#ifdef MULTI_PROCESS_SUPPORT
#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/interprocess/sync/named_upgradable_mutex.hpp>
#include <boost/interprocess/sync/sharable_lock.hpp>
#include <boost/interprocess/sync/scoped_lock.hpp>
using boost::interprocess::managed_shared_memory;
using boost::interprocess::create_only;
using boost::interprocess::open_only;
using boost::interprocess::named_upgradable_mutex;
using boost::interprocess::sharable_lock;
using boost::interprocess::scoped_lock;

static managed_shared_memory *segment;

static FGDataAllocator *fg_map_allocator;
static FGDataMap *fg_data_map;
static named_upgradable_mutex *fg_data_map_lock;

static bool creator = false;

void ipc_shmem_init(bool create)
{
    // only one process should pass true here (for intnw, the scout)
    creator = create;

    // construct shared memory segment and data structures
    if (creator) {
        shared_memory_object::remove(INTNW_SHMEM_NAME);
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
    fg_data_map = segment->find_or_construct<FGDataMap>(INTNW_SHMEM_MAP_NAME)(*fg_map_allocator); // never constructs; scout is first
}

void ipc_shmem_deinit()
{
    // scout takes care of deallocating shared memory objects when it exits

    // clean up process-local data structures
    delete fg_data_map_lock;
    delete fg_map_allocator;
    delete segment;

    if (creator) {
        shared_memory_object::remove(INTNW_SHMEM_NAME);
    }
}

static FGDataPtr map_lookup(struct in_addr ip_addr)
{
    FGDataPtr fg_data;

    sharable_lock read_lock(*fg_data_map_lock);
    if (fg_data_map->count(ip_addr) > 0) {
        fg_data.reset((*fg_data_map)[ip_addr]);
    }

    return fg_data;
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

void ipc_update_fg_timestamp(struct in_addr ip_addr,
                             gint tv_sec)
{
    FGDataPtr fg_data = map_lookup(ip_addr);
    if (fg_data) {
        g_atomic_int_set(&fg_data->last_fg_tv_sec, tv_sec);
    }
}

void ipc_increment_fg_senders(struct in_addr ip_addr)
{
    FGDataPtr fg_data = map_lookup(ip_addr);
    if (fg_data) {
        g_atomic_int_inc(&fg_data->num_fg_senders);
    }
}

void ipc_decrement_fg_senders(struct in_addr ip_addr)
{
    FGDataPtr fg_data = map_lookup(ip_addr);
    if (fg_data) {
        (void)g_atomic_int_dec_and_test(&fg_data->num_fg_senders);
    }
}

void add_iface(struct in_addr ip_addr)
{
    scoped_lock lock(*fg_data_map_lock);
    // TODO: pick up here.
}

void remove_iface(struct in_addr ip_addr)
{
    scoped_lock lock(*fg_data_map_lock);
    
}
// TODO: and then test it out!


#endif
