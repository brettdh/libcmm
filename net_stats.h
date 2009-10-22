#ifndef net_stats_h_incl
#define net_stats_h_incl

#include <netinet/in.h>
#include <sys/types.h>
#include <time.h>
#include <pthread.h>
#include "libcmm_irob.h"

// each CSocket will include an object of this type, since the stats
// are kept for each (local,remote) interface pair.
class NetStats {
  public:
    // Retrieve the current estimate for the three parameters of interest.
    //  Caller may pass NULL if it is not interested in one or more
    //  of the estimates.
    // Returns true on success; false if there isn't sufficient
    //  history to compute an estimate.
    bool get_estimate(u_long *latency,
                      u_long *bandwidth_up,
                      u_long *bandwidth_down);

    // CSocketSender should call this immediately before it sends
    //  bytes related to an IROB.  The bytes argument should include
    //  the total number of bytes passed to the system call.
    void report_send_event(irob_id_t irob_id, size_t bytes);
    
    // CSocketSender should call this immediately before it sends bytes
    //  UNrelated to an IROB.  This is needed to compute queuing delays
    //  for IROB-related messages that are queued behind non-IROB-related
    //  messages.
    void report_send_event(size_t bytes);

    // CSocketReceiver should call this immediately after it receives
    //  the ACK for an IROB.  The srv_time argument should be the
    //  "service time" reported with the ACK.
    void report_ack(irob_id_t irob_id, struct timeval srv_time);

    NetStats();    
  private:
    void add_queuing_delay(irob_id_t irob_id);

    struct in_addr local_addr;
    struct in_addr remote_addr;

    // Enforces safe concurrent accesses and atomic updates of stats
    pthread_rwlock_t lock;
};

#endif
