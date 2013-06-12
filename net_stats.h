#ifndef net_stats_h_incl
#define net_stats_h_incl

#include "net_interface.h"
#include <time.h>
#include <pthread.h>
#include "pthread_util.h"
#include "libcmm_irob.h"
#include "intset.h"
#include "estimate.h"
#include <map>

class NetworkChooser;

class QueuingDelay {
  public:
    /* Compute the queuing delay of a message based on the 
     * queuing delay of the previous message, its
     * arrival time, and the arrival time and size of the
     * current message.
     */
    struct timeval add_message(size_t msg_size, u_long bw_estimate,
                               irob_id_t last_irob_id = -1);

    // returns the earliest time that a message could follow 
    //  the previous message:
    //     last_msg_time + last_msg_qdelay + 
    //     (last_msg_size/last_bw_estimate)
    struct timeval get_queuable_time(irob_id_t irob_id) const;

    bool caused_by(irob_id_t irob_id) const;

    QueuingDelay();
  private:
    struct timeval last_msg_time;
    struct timeval last_msg_qdelay;
    size_t last_msg_size;
    u_long last_bw_estimate;
    irob_id_t last_irob;
};

class IROBMeasurement {
  public:
    IROBMeasurement();
    void set_id(irob_id_t id_);

    void add_bytes(size_t bytes, struct timeval queuable_time,
                   u_long bw_est);
    void add_delay(struct timeval delay);
    void ack(struct timeval *real_time = NULL);
    void mark_failed();

    // don't call until after calling ack()
    struct timeval RTT();
    size_t num_bytes();

    bool is_finished() const {
        return finished;
    }
    void finish() {
        finished = true;
    };
  private:
    /* total of all IROB-related
     * bytes sent, including headers */
    irob_id_t id;
    size_t total_size;
    struct timeval arrival_time;
    struct timeval last_activity;
    struct timeval ack_time;
    struct timeval failure_time;

    /* includes both queuing delay due to self-interference time
     * and time between send calls for this IROB */
    struct timeval total_delay;

    // If I see another IROB's bytes after my own, I assume that I've
    //  seen all my bytes and set finished=true.  If this turns out later
    //  to be false, I'll just throw out this measurement to avoid the
    //  nasty subtleties in the queuing delay compensation.
    bool finished;
};


// estimate types, for use with get_estimate
#define NET_STATS_LATENCY 0
#define NET_STATS_BW_UP   1
#define NET_STATS_BW_DOWN 2

#define NUM_ESTIMATES 3

// data needed to re-start passive measurement calculation
//  from previous measurements.
struct estimate_set {
    Estimate estimates[NUM_ESTIMATES];
    bool error_estimators_initialized;
    
    struct timeval last_RTT;
    struct timeval last_srv_time;
    size_t last_req_size;

    irob_id_t last_irob;
};

// each CSocket will include an object of this type, since the stats
// are kept for each (local,remote) interface pair.
class NetStats {
  public:
    // Retrieve the current estimate for the parameter of interest.
    // Returns true on success; false if there isn't sufficient
    //  history to compute the estimate, or if either argument
    //  is invalid.
    bool get_estimate(unsigned short type, u_long& value);

    static bool get_estimate(const struct net_interface& local_iface, 
                             const struct net_interface& remote_iface,
                             unsigned short type, u_long& value);
      
    // CSocketSender should call this immediately before it sends
    //  bytes related to an IROB.  The bytes argument should include
    //  the total number of bytes passed to the system call.
    void report_irob_send_event(irob_id_t irob_id, size_t bytes);
    
    // CSocketSender should call this immediately before it sends
    //  bytes UNrelated to an IROB.  This is needed to compute queuing
    //  delays for IROB-related messages that are queued behind
    //  non-IROB-related messages.  
    // If qdelay is non-NULL, writes into qdelay the queuing delay of
    //  an ack to be sent now.  We calculate this at the sender rather
    //  than the receiver because we only estimate upstream bandwidth.
    void report_non_irob_send_event(size_t bytes, struct timeval *qdelay = NULL);

    // CSocketReceiver should call this immediately before it receives bytes
    //  UNrelated to an IROB.  This is needed to compute queuing delays
    //  for IROB-related messages that are queued behind ACKs.
    //void report_recv_event(size_t bytes); // XXX: not being used.

    // CSocketReceiver should call this immediately after it receives
    //  the ACK for an IROB.  The srv_time argument should be the
    //  "service time" reported with the ACK.
    // if real_time is non-NULL, it will be reported as the ACK time
    //  instead of the current result of gettimeofday.
    //  This makes sense when a flurry of ACKs arrive at once; 
    //  we want to say that they all arrived at about the same time.
    // If there was a new measurement, store its values into bw_out
    //  and latency_seconds_out, and return true.  If not, return false.
    bool report_ack(irob_id_t irob_id, struct timeval srv_time,
                    struct timeval ack_qdelay, 
                    struct timeval *real_time = NULL,
                    double *bw_out = NULL, double *latency_seconds_out = NULL);

    //  if there are any unACKed IROBs, 
    //  add a latency measurement equal to the time since
    //  the first of them was sent.  This captures failover delay.
    bool mark_irob_failures(NetworkChooser *chooser, int network_type,
                            double *latency_seconds_out=NULL);

    // report the total number of bytes to be sent on the network 
    //  in delivering this IROB.
    // used for determining whether an IROB was striped.
    void report_total_irob_bytes(irob_id_t irob_id, size_t total_bytes);

    // remove this IROB without adding a new measurement.
    void remove(irob_id_t irob_id);

    // The remote host computes its upstream b/w and sends it to
    //  me; it's my downstream b/w.  CSocketReceiver should
    //  call this when it receives such a message.
    // This value will be treated as a spot value, not as a 
    //  smoothed estimate.
    void report_bw_down(u_long bw_down);

    // Add an external spot observation (from the scout).
    void update(struct net_interface local_iface,
                struct net_interface remote_iface,
                NetworkChooser *network_chooser,
                int network_type);

    NetStats(struct net_interface local_iface, 
             struct net_interface remote_iface);
    ~NetStats();

    // for testing.  reset all the stats to as when the program started.
    static void resetAll();

    void getStats(NetworkChooser *network_chooser, int network_type);

    static void get_time(struct timeval &tv);

    // pseudo-mocking interface for getting time.
    //  useful for testing.
    // TODO: use a real mocking framework for this?
    typedef int (*time_getter_fn_t)(struct timeval *, void *);
    static void set_time_getter(time_getter_fn_t new_gettimeofday);
  private:
    static time_getter_fn_t time_getter;

    struct in_addr local_addr;
    struct in_addr remote_addr;

    // Enforces safe concurrent accesses and atomic updates of stats
    RWLOCK_T my_lock;

    QueuingDelay outgoing_qdelay;
    //QueuingDelay incoming_qdelay;

    //Estimate net_estimates[NUM_ESTIMATES];
    struct estimate_set net_estimates;

    struct timeval last_RTT;
    struct timeval last_srv_time;
    size_t last_req_size;

    irob_id_t last_irob;
    
    typedef std::map<irob_id_t, IROBMeasurement> irob_measurements_t;
    irob_measurements_t irob_measurements;

    IntSet past_irobs;

    typedef std::map<std::pair<struct net_interface, 
                               struct net_interface>,
                     struct estimate_set> StatsCache;
    static StatsCache *stats_cache;
    static RWLOCK_T *stats_cache_lock;
    void cache_save();
    bool cache_restore();

    // return true iff the stats for this (local,remote) interface pair
    //  are already in the cache.
    bool stats_are_cached();

    bool error_estimators_initialized;

    class IROBTransfers;
    static IROBTransfers *irob_transfers;
    static IntSet *striped_irobs;
    static pthread_mutex_t irob_transfers_lock; // protects irob_transfers, striped_irobs

    struct static_initializer {
        static_initializer();
    };
    static static_initializer init;
};

void update_EWMA(double& EWMA, double spot, double gain);

#endif
