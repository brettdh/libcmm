#include "net_stats.h"
#include <pthread.h>
#include <netinet/in.h>
#include <sys/types.h>


NetStats::NetStats(struct ip_addr local_addr_, 
                   struct ip_addr remote_addr_)
    : local_addr(local_addr_), remote_addr(remote_addr_)
{
    pthread_rwlock_init(&lock);
}

bool 
NetStats::get_estimate(u_long *latency,
                       u_long *bandwidth_up,
                       u_long *bandwidth_down)
{
    
}

void 
NetStats::report_send_event(irob_id_t irob_id, size_t bytes)
{

}

void 
NetStats::report_send_event(size_t bytes)
{

}

void 
NetStats::report_ack(irob_id_t irob_id, struct timeval srv_time)
{

}

void
NetStats::add_queuing_delay(irob_id_t irob_id)
{
    
}
