#include "net_stats.h"
#include "debug.h"
#include <pthread.h>
#include "pthread_util.h"
#include <netinet/in.h>

#include "libcmm_net_restriction.h"
#include "network_chooser.h"
#ifndef CMM_UNIT_TESTING
#include "intnw_config.h"
#endif

// tcp.h is temperamental.
#include <netinet/tcp.h>
//#include <linux/tcp.h>

#include <sys/types.h>
#include "timeops.h"
#include <cmath>
#include <map>
#include <sstream>
using std::pair; using std::make_pair;
using std::ostringstream;

class InvalidEstimateException {};

NetStats::StatsCache *NetStats::stats_cache = NULL;
RWLOCK_T *NetStats::stats_cache_lock = NULL;
NetStats::static_initializer NetStats::init;

NetStats::IROBTransfers *NetStats::irob_transfers = NULL;
IntSet *NetStats::striped_irobs = NULL;
pthread_mutex_t NetStats::irob_transfers_lock = MY_PTHREAD_MUTEX_INITIALIZER;

class IROBTransfersPerNetwork {
public:
    IROBTransfersPerNetwork();
    void addTransfer(struct in_addr local_addr_, 
                     struct in_addr remote_addr_,
                     size_t bytes);
    bool hasAllBytes(struct in_addr local_addr_, 
                     struct in_addr remote_addr_);
    void reportTotalBytes(size_t total_bytes);
    bool remove(struct in_addr local_addr,
                struct in_addr remote_addr);
private:
    size_t total_bytes;

    typedef std::map<std::pair<in_addr_t, in_addr_t>, size_t> map_type_t;
    map_type_t transfers_by_network;

    size_t& getValueRef(struct in_addr local_addr,
                        struct in_addr remote_addr);
};

class NetStats::IROBTransfers {
public:
    void addTransfer(irob_id_t id, 
                     struct in_addr local_addr,
                     struct in_addr remote_addr,
                     size_t bytes);
    bool hasAllBytes(irob_id_t id,
                     struct in_addr local_addr,
                     struct in_addr remote_addr);
    void reportTotalBytes(irob_id_t id, size_t total_bytes);
    void removeAll(struct in_addr local_addr,
                   struct in_addr remote_addr);
private:
    typedef std::map<irob_id_t, IROBTransfersPerNetwork> IROBIfaceMap;
    IROBIfaceMap irob_iface_transfers;
};


NetStats::static_initializer::static_initializer()
{
    resetAll();
}

void
NetStats::resetAll()
{
    delete stats_cache;
    delete stats_cache_lock;
    delete irob_transfers;
    delete striped_irobs;

    stats_cache = new StatsCache;
    stats_cache_lock = new RWLOCK_T;
    RWLOCK_INIT(NetStats::stats_cache_lock, NULL);

    irob_transfers = new IROBTransfers;
    striped_irobs = new IntSet;
}


static struct timeval time_to_send(size_t msg_size, u_long bw_estimate)
{
    double bw_seconds = ((double)msg_size) / bw_estimate;
    useconds_t bw_time_usecs = (useconds_t)(bw_seconds * 1000000);
    struct timeval bw_time = convert_to_timeval(bw_time_usecs);
    return bw_time;
}


NetStats::NetStats(struct net_interface local_iface, 
                   struct net_interface remote_iface)
    : local_addr(local_iface.ip_addr), remote_addr(remote_iface.ip_addr),
      name(net_type_name(get_network_type(local_iface, remote_iface))),
      error_estimators_initialized(false)
{
    RWLOCK_INIT(&my_lock, NULL);
    last_RTT.tv_sec = last_srv_time.tv_sec = -1;
    last_RTT.tv_usec = last_srv_time.tv_usec = 0;
    last_req_size = 0;
    last_irob = -1;

    net_estimates.estimates.assign({
        FlipFlopEstimate(name + "_latency"),
        FlipFlopEstimate(name + "_bw_up"), 
        FlipFlopEstimate(name + "_bw_down")
    });

    
    // don't do this until cache-by-BSSID is implemented.
    //  the last WiFi estimates don't predict the next ones.
    // anyway, the 3G stats stay as long as that CSocket does,
    //  which should be the entire trace.
    //cache_restore();

    // make this stats initialization only happen
    // when the multisocket is initially created.
    // it should not happen every time a network comes up,
    // unless I have actually made a new active measurement.
    // XXX: *** Actually, this should probably be decided
    // XXX: *** in the scout, though it needs some protocol to 
    // XXX: *** tell the application that there's a new network
    // XXX: *** but no new measurements.

    bool init_stats = false;
#ifndef CMM_UNIT_TESTING
    init_stats = Config::getInstance()->getUseBreadcrumbsEstimates();
#endif
    if (!init_stats) {
        // only initialize the first time
        init_stats = !stats_are_cached();
        if (!init_stats) {
            // after the first observation per network,
            // restore the saved observations
            cache_restore();
        }
    }
    
    if (init_stats) {
        u_long init_bandwidth = iface_bandwidth(local_iface, remote_iface);
        u_long init_latency = iface_RTT(local_iface, remote_iface) / 2;
        if (init_bandwidth > 0) {
            dbgprintf("Adding initial bandwidth observation: %lu bytes/sec\n",
                      init_bandwidth);
            net_estimates.estimates[NET_STATS_BW_UP].add_observation(init_bandwidth);
        }
        if (init_latency > 0) {
            dbgprintf("Adding initial latency observation: %lu ms\n",
                      init_latency);
            net_estimates.estimates[NET_STATS_LATENCY].add_observation(init_latency);
        }
    }
    cache_save();
}

void
NetStats::getStats(NetworkChooser *network_chooser, int network_type)
{
#ifndef CMM_UNIT_TESTING
    if (Config::getInstance()->getUseBreadcrumbsEstimates() || !error_estimators_initialized) {
        // only update the error if we're using breadcrumbs network estimates.
        // otherwise the estimate isn't new since the last passive measurement.
        
        u_long bw_est, latency_est;
        if (net_estimates.estimates[NET_STATS_BW_UP].get_estimate(bw_est) &&
            net_estimates.estimates[NET_STATS_LATENCY].get_estimate(latency_est)) {
            double latency_seconds = latency_est / 1000.0;
            network_chooser->reportNetStats(network_type, 
                                            bw_est, bw_est,
                                            latency_seconds, latency_seconds);

            if (!error_estimators_initialized) {
                cache_save(); // just to save the fact that I already initialized it
                error_estimators_initialized = true;
            }
        }
    }
#endif
}

void
NetStats::update(struct net_interface local_iface,
                 struct net_interface remote_iface,
                 NetworkChooser *network_chooser,
                 int network_type)
{
    {
        PthreadScopedRWLock lock(&my_lock, true);

        u_long spot_bandwidth = iface_bandwidth(local_iface, remote_iface);
        u_long spot_latency = iface_RTT(local_iface, remote_iface) / 2;
        if (spot_bandwidth > 0) {
            dbgprintf("Adding bandwidth observation from scout: %lu bytes/sec\n",
                      spot_bandwidth);
            net_estimates.estimates[NET_STATS_BW_UP].add_observation(spot_bandwidth);
            //net_estimates.estimates[NET_STATS_BW_UP].reset(spot_bandwidth);
        }
        if (spot_latency > 0) {
            dbgprintf("Adding latency observation from scout: %lu ms\n",
                      spot_latency);
            net_estimates.estimates[NET_STATS_LATENCY].add_observation(spot_latency);
            //net_estimates.estimates[NET_STATS_LATENCY].reset(spot_latency);
        }
        if (network_chooser && spot_bandwidth > 0 && spot_latency > 0) {
#ifndef CMM_UNIT_TESTING
            u_long bw_est, latency_est;
            if (net_estimates.estimates[NET_STATS_BW_UP].get_estimate(bw_est) &&
                net_estimates.estimates[NET_STATS_LATENCY].get_estimate(latency_est)) {
                double spot_latency_seconds = spot_latency / 1000.0;
                double latency_est_seconds = latency_est / 1000.0;
                network_chooser->reportNetStats(network_type, 
                                                spot_bandwidth, bw_est,
                                                spot_latency_seconds, latency_est_seconds);
            }
#endif
        }
    }
    cache_save();
}


NetStats::~NetStats()
{
    cache_save();

    PthreadScopedLock lock(&irob_transfers_lock);
    irob_transfers->removeAll(local_addr, remote_addr);
}

void
NetStats::cache_save()
{
    PthreadScopedRWLock wrlock(stats_cache_lock, true);
    PthreadScopedRWLock rd_self_lock(&my_lock, false);

    struct net_interface local_iface, remote_iface;
    local_iface.ip_addr = local_addr;
    remote_iface.ip_addr = remote_addr;
    StatsCache::key_type key = make_pair(local_iface, remote_iface);
    struct estimate_set& cached_estimates = (*stats_cache)[key];
    cached_estimates.estimates = net_estimates.estimates;
    
    cached_estimates.error_estimators_initialized = error_estimators_initialized;

    cached_estimates.last_RTT = last_RTT;
    cached_estimates.last_srv_time = last_srv_time;
    cached_estimates.last_req_size = last_req_size;
    cached_estimates.last_irob = last_irob;
}


bool
NetStats::stats_are_cached()
{
    PthreadScopedRWLock rdlock(stats_cache_lock, false);
    PthreadScopedRWLock wr_self_lock(&my_lock, false);

    struct net_interface local_iface, remote_iface;
    local_iface.ip_addr = local_addr;
    remote_iface.ip_addr = remote_addr;
    StatsCache::key_type key = make_pair(local_iface, remote_iface);
    return (stats_cache->count(key) > 0);
}

bool
NetStats::cache_restore()
{
    PthreadScopedRWLock rdlock(stats_cache_lock, false);
    PthreadScopedRWLock wr_self_lock(&my_lock, true);

    struct net_interface local_iface, remote_iface;
    local_iface.ip_addr = local_addr;
    remote_iface.ip_addr = remote_addr;
    StatsCache::key_type key = make_pair(local_iface, remote_iface);
    if (stats_cache->count(key) == 0) {
        // no stats for this pair
        return false;
    }

    struct estimate_set& cached_estimates = (*stats_cache)[key];
    for (size_t i = 0; i < NUM_ESTIMATES; ++i) {
        u_long value;
        if (cached_estimates.estimates[i].get_estimate(value)) {
            net_estimates.estimates[i] = cached_estimates.estimates[i];
        }
    }
    error_estimators_initialized = cached_estimates.error_estimators_initialized;

    last_RTT = cached_estimates.last_RTT;
    last_srv_time = cached_estimates.last_srv_time;
    last_req_size = cached_estimates.last_req_size;
    last_irob = cached_estimates.last_irob;
    
    return true;
}

bool 
NetStats::get_estimate(const struct net_interface& local_iface, 
                       const struct net_interface& remote_iface,
                       unsigned short type, u_long& value)
{
    if (type >= NUM_ESTIMATES) {
        return false;
    }

    PthreadScopedRWLock rdlock(stats_cache_lock, false);

    StatsCache::key_type key = make_pair(local_iface, remote_iface);
    StatsCache::iterator pos = stats_cache->find(key) ;
    if (pos != stats_cache->end()) {
        return pos->second.estimates[type].get_estimate(value);
    }
    return false;
}

bool 
NetStats::get_estimate(unsigned short type, u_long& value)
{
    if (type >= NUM_ESTIMATES) {
        return false;
    }

    PthreadScopedRWLock lock(&my_lock, false);

    return net_estimates.estimates[type].get_estimate(value);
}

void
NetStats::report_total_irob_bytes(irob_id_t irob_id, size_t total_bytes_sent)
{
    PthreadScopedLock lock(&irob_transfers_lock);
    irob_transfers->reportTotalBytes(irob_id, total_bytes_sent);
}

void 
NetStats::report_irob_send_event(irob_id_t irob_id, size_t bytes)
{
    // check whether any other NetStats objects have seen this
    //   IROB; that would mean that it's been striped and we should
    //   probably disregard estimates based on it
    bool irob_was_striped = false;
    {
        PthreadScopedLock lock(&irob_transfers_lock);
        
        if (striped_irobs->contains(irob_id)) {
            irob_was_striped = true;
        } else {
            irob_transfers->addTransfer(irob_id, 
                                        local_addr, 
                                        remote_addr,
                                        bytes);
        }
    }
    
    PthreadScopedRWLock lock(&my_lock, true);

    if (past_irobs.contains(irob_id)) {
        dbgprintf("Ignoring send event from previously-ignored IROB %ld\n",
                  irob_id);
        return;
    }

    if (irob_was_striped) {
        dbgprintf("IROB %ld has been striped; ignoring estimation\n", irob_id);
        irob_measurements.erase(irob_id);
        return;
    }

    u_long bw_est = 0;
    (void)net_estimates.estimates[NET_STATS_BW_UP].get_estimate(bw_est);
    
    if (irob_id != last_irob) {
        if (irob_measurements.find(last_irob) != irob_measurements.end()) {
            irob_measurements[last_irob].finish();
        }
        last_irob = irob_id;
    }

    IROBMeasurement& measurement = irob_measurements[irob_id];
    measurement.set_id(irob_id); // in case it is new
    if (measurement.is_finished()) {
        dbgprintf("Saw an interleaving for IROB %ld; "
                  "ignoring and removing it\n",
                  irob_id);
        irob_measurements.erase(irob_id);
        past_irobs.insert(irob_id);
        return;
    }

    struct timeval queuable_time = outgoing_qdelay.get_queuable_time(irob_id);
    // queuable_time is the earliest time that this message 
    //  could hit the network (or else it's {0, 0}, if
    //  the last message was from a different IROB; that is,
    //  if the message could go immediately)

    measurement.add_bytes(bytes, queuable_time, bw_est); // accounts for inter-send delay

    // don't add queuing delay between parts of an IROB, since 
    //  that delay doesn't come into the IROB's RTT measurement
    // QueuingDelay class takes care of this now; returns 0.0 if irob_id is the
    //  same as the last one
    struct timeval qdelay = outgoing_qdelay.add_message(bytes, bw_est, irob_id);
    dbgprintf("Adding %lu.%06lu queuing delay to IROB %ld\n",
              qdelay.tv_sec, qdelay.tv_usec, irob_id);
    measurement.add_delay(qdelay);
}

void 
NetStats::report_non_irob_send_event(size_t bytes, struct timeval *qdelay)
{
    PthreadScopedRWLock lock(&my_lock, true);
    u_long bw_est = 0;
    (void)net_estimates.estimates[NET_STATS_BW_UP].get_estimate(bw_est);
    struct timeval my_qdelay = outgoing_qdelay.add_message(bytes, bw_est);
    if (qdelay) {
        *qdelay = my_qdelay;
    }
}

#if 0
// XXX: this is not being used.  Moreover, since my BW_DOWN estimate is bogus 
//  and unused, I need the ACK-sender to calculate the queuing delay on the ACKs
//  and report it.  Maybe that will tighten up my estimation?
void
NetStats::report_recv_event(size_t bytes)
{
    PthreadScopedRWLock lock(&my_lock, true);
    u_long bw_est = 0;
    (void)net_estimates.estimates[NET_STATS_BW_DOWN].get_estimate(bw_est);
    (void)incoming_qdelay.add_message(bytes, bw_est);
}
#endif

// latency_RTT is the RTT that has essentially no bandwidth component.
// bw_RTT is the RTT that has a non-negligible bandwidth component.
//  latency_srv_time and bw_srv_time are the service times associated 
//  with those RTTs.
// bw_req_size is the request size associated with bw_RTT.
static void
calculate_bw_latency(struct timeval latency_RTT, struct timeval bw_RTT,
                     struct timeval latency_srv_time, struct timeval bw_srv_time, 
                     size_t bw_req_size, double& bw, double& latency)
{
    bw = 0.0;
    latency = 0.0;
    
    // Treat size as if it were zero, since the bandwidth
    //  component of the RTT is so small
    // Then the calculation becomes very simple:
    //   latency = (RTT - service) / 2
    if (timercmp(&latency_RTT, &latency_srv_time, >)) {
        struct timeval diff;
        timersub(&latency_RTT, &latency_srv_time, &diff);
        latency = (convert_to_useconds(diff) / 1000.0) / 2.0; // ms
    } // else: invalid measurement, ignore
    
    if (latency > 0.0) {
        //  bw = last_req_size / (last_RTT - 2*latency - lastservice)
        struct timeval diff;
        if (timercmp(&bw_RTT, &bw_srv_time, >)) {
            timersub(&bw_RTT, &bw_srv_time, &diff);
            bw = (((double)bw_req_size) / 
                  ((convert_to_useconds(diff)/1000000.0) - (2.0*latency/1000.0)));
        } // else: invalid measurement, ignore
    }
}

// returns true if a new measurement was obtained
bool
NetStats::report_ack(irob_id_t irob_id, struct timeval srv_time,
                     struct timeval ack_qdelay, 
                     struct timeval *real_time,
                     double *bw_out, double *latency_seconds_out)
{
    bool new_measurement = false;
    
    if (srv_time.tv_usec == -1) {
        /* the original ACK was dropped, so it's not wise to use 
         * this one for a measurement, since the srv_time is
         * invalid. */
        dbgprintf("Got ACK for IROB %ld, but srv_time is invalid. Ignoring.\n",
                  irob_id);
        return false;
    }

    {
        bool striped = false;
        PthreadScopedLock lock(&irob_transfers_lock);
        if (striped_irobs->contains(irob_id)) {
            striped = true;
        } else if (!irob_transfers->hasAllBytes(irob_id, 
                                                local_addr,
                                                remote_addr)) {
            striped = true;
            striped_irobs->insert(irob_id);
        }
        if (striped) {
            dbgprintf("Got ACK for IROB %ld, but it was striped.  Ignoring.\n",
                      irob_id);
            return false;
        }
    }

    {
        PthreadScopedRWLock lock(&my_lock, true);
        if (irob_measurements.find(irob_id) == irob_measurements.end()) {
            dbgprintf("Got ACK for IROB %ld, but I've forgotten it.  Ignoring.\n",
                      irob_id);
            return false;
        }

        IROBMeasurement measurement = irob_measurements[irob_id];
        irob_measurements.erase(irob_id);

        measurement.add_delay(ack_qdelay);
        measurement.ack(real_time);
    
        struct timeval RTT;

        try {
            RTT = measurement.RTT();
        } catch (const InvalidEstimateException &e) {
            dbgprintf("Invalid measurement detected; ignoring\n");
            return false;
        }

        size_t req_size = measurement.num_bytes();

        dbgprintf("Reporting new ACK for IROB %ld; RTT %lu.%06lu  "
                  "srv_time %lu.%06lu qdelay %lu.%06lu size %zu\n",
                  irob_id, RTT.tv_sec, RTT.tv_usec, 
                  srv_time.tv_sec, srv_time.tv_usec,
                  ack_qdelay.tv_sec, ack_qdelay.tv_usec,
                  req_size);

        if (last_RTT.tv_sec != -1) {
            // solving simple system of 2 linear equations, from paper
            /* We want this:
             * u_long bw_up_estimate = ((req_size - last_req_size) /
             *                          (RTT - last_RTT + 
             *                           last_srv_time - srv_time));
             * but done with struct timevals to avoid overflow.
             */

            // XXX: this code is gross.  Better to just write timeops
            // that work for negative values.

            u_long bw_est = 0, latency_est = 0;
            u_long bw_obs = 0, latency_obs = 0;
            bool valid_result = false;
            bool bw_valid = false, lat_valid = false;

            //const size_t MIN_SIZE_FOR_BW_ESTIMATE = 1500; // ethernet MTU


            // we get really bad bandwidth measurements when we use a 1-packet
            // threshold.  So, for small transfers, we will treat any network
            // performance change as being caused solely by latency.  This is
            // usually okay, since what we really care about is total network transfer time.
            const size_t MIN_SIZE_FOR_BW_ESTIMATE = 3000; // 2 packets; probably needs tuning


            size_t size_diff = ((req_size > last_req_size)
                                ? (req_size - last_req_size)
                                : (last_req_size - req_size));

            // override; testing whether this limit is necessary.
            // result: decided that the resulting measurements
            //         are wildly wrong.  I'd need to think more carefully
            //         about how to measure bandwidth with small transfers
            //         if I were to try this.  For now, I'll leave these
            //         limits in place.
            /* size_diff = MIN_SIZE_FOR_BW_ESTIMATE; */
            
            if (size_diff >= MIN_SIZE_FOR_BW_ESTIMATE && 
                (req_size >= MIN_SIZE_FOR_BW_ESTIMATE ||
                 last_req_size >= MIN_SIZE_FOR_BW_ESTIMATE)) {
                if (req_size < MIN_SIZE_FOR_BW_ESTIMATE) {
                    ASSERT(last_req_size >= MIN_SIZE_FOR_BW_ESTIMATE);
                    double bw = 0.0, latency = 0.0;
                    calculate_bw_latency(RTT, last_RTT, srv_time, last_srv_time,
                                         last_req_size, bw, latency);
                    
                    bw_obs = round_nearest(bw);
                    latency_obs = round_nearest(latency);
                    bw_valid = (bw > 0.0);
                    lat_valid = (latency > 0.0);
                    valid_result = (bw_valid || lat_valid);
                } else if (last_req_size < MIN_SIZE_FOR_BW_ESTIMATE) {
                    ASSERT(req_size >= MIN_SIZE_FOR_BW_ESTIMATE);
                    double bw = 0.0, latency = 0.0;
                    calculate_bw_latency(last_RTT, RTT, last_srv_time, srv_time,
                                         req_size, bw, latency);
                    
                    bw_obs = round_nearest(bw);
                    latency_obs = round_nearest(latency);
                    bw_valid = (bw > 0.0);
                    lat_valid = (latency > 0.0);
                    valid_result = (bw_valid || lat_valid);
                } else {
                    // both contribute substantially to bw cost, and
                    // they differ sufficiently
                    size_t numerator = ((req_size > last_req_size)
                                        ? (req_size - last_req_size)
                                        : (last_req_size - req_size));
                    
                    struct timeval denominator;
                    struct timeval RTT_diff, srv_time_diff;
                    bool RTT_diff_pos = timercmp(&last_RTT, &RTT, <);
                    if (RTT_diff_pos) {
                        TIMEDIFF(last_RTT, RTT, RTT_diff);
                    } else {
                        TIMEDIFF(RTT, last_RTT, RTT_diff);
                    }
                    bool srv_time_diff_pos = timercmp(&last_srv_time, &srv_time, <);
                    if (srv_time_diff_pos) {
                        TIMEDIFF(last_srv_time, srv_time, srv_time_diff);
                    } else {
                        TIMEDIFF(srv_time, last_srv_time, srv_time_diff);
                    }
                    
                    if ((RTT_diff_pos && srv_time_diff_pos) ||
                        !(RTT_diff_pos || srv_time_diff_pos)) {
                        timeradd(&RTT_diff, &srv_time_diff, &denominator);
                    } else if (RTT_diff_pos) {
                        timersub(&RTT_diff, &srv_time_diff, &denominator);
                    } else if (srv_time_diff_pos) {
                        timersub(&RTT_diff, &srv_time_diff, &denominator);
                    } else ASSERT(0);
                    
                    // get bandwidth estimate in bytes/sec, rather than bytes/usec
                    double bw = ((double)numerator / convert_to_useconds(denominator)) * 1000000.0;
                    
                    /* latency = ((RTT - srv_time)/1000 - (req_size/bw)*1000); */
                    struct timeval diff;
                    if (timercmp(&RTT, &srv_time, >)) {
                        timersub(&RTT, &srv_time, &diff);
                    } else {
                        diff.tv_sec = 0;
                        diff.tv_usec = 0;
                    }
                    double latency = (convert_to_useconds(diff)/1000.0 - 
                                      (req_size / bw * 1000.0)) / 2.0;
                    
                    bw_obs = round_nearest(bw);
                    latency_obs = round_nearest(latency);

                    bw_valid = (bw > 0);
                    lat_valid = (latency > 0);
                    valid_result = (bw_valid && lat_valid);
                }
                if (!valid_result) {
                    dbgprintf("Spot values indicate invalid observation; ignoring\n");
                }
            } else if(net_estimates.estimates[NET_STATS_BW_UP].get_estimate(bw_est)) {
                /* latency = ((RTT - srv_time)/1000 - (req_size/bw)*1000); */
                double bw = static_cast<double>(bw_est);
                struct timeval diff;
                if (timercmp(&RTT, &srv_time, >)) {
                    timersub(&RTT, &srv_time, &diff);
                } else {
                    diff.tv_sec = 0;
                    diff.tv_usec = 0;
                }
                double latency = (convert_to_useconds(diff)/1000.0 - 
                                  (req_size / bw * 1000.0)) / 2.0;
                latency_obs = round_nearest(latency);

                // TODO: prevent this from being a NEW bandwidth estimate (because it isn't)
                // TODO: also rethink the merit of the size check.  perhaps compare the size
                // TODO:  to the bandwidth measurement and see whether it's sane?

                bw_valid = false; // because it's not a new observation
                lat_valid = (latency > 0);
                valid_result = lat_valid;
                if (!valid_result) {
                    dbgprintf("Spot values indicate invalid observation; ignoring\n");
                }
            } else {
                dbgprintf("Couldn't produce spot values; equal message "
                          "sizes and no prior bw estimate\n");
            }

            if (valid_result) {
                ostringstream s;
                s << "New spot values: ";
                if (bw_valid) {
                    net_estimates.estimates[NET_STATS_BW_UP].add_observation(bw_obs);
                    s << "bw " << bw_obs;
                }
                if (lat_valid) {
                    net_estimates.estimates[NET_STATS_LATENCY].add_observation(latency_obs);
                    s << " latency " << latency_obs;
                }
                s << "\n";
                dbgprintf("%s", s.str().c_str());
            
                if (bw_out) *bw_out = bw_obs;
                if (latency_seconds_out) *latency_seconds_out = (latency_obs / 1000.0);

                s.str();
                s << "New estimates: bw_up ";
                if (net_estimates.estimates[NET_STATS_BW_UP].get_estimate(bw_est)) {
                    s << bw_est << " bytes/sec, ";
                } else {
                    s << "(invalid), ";
                }
                if (net_estimates.estimates[NET_STATS_LATENCY].get_estimate(latency_est)) {
                    s << "latency " << latency_est << " ms";
                } else {
                    s << "latency (invalid)";
                }
                s << "\n";
                dbgprintf("%s", s.str().c_str());

                // TODO: send bw_up estimate to remote peer as its bw_down.  Or maybe do that
                //       in CSocketReceiver, after calling this.
                
                new_measurement = true;
            }
        }

        last_RTT = RTT;
        last_req_size = req_size;
        last_srv_time = srv_time;
    }
    cache_save();
    return new_measurement;
}

bool
NetStats::mark_irob_failures(NetworkChooser *chooser, int network_type,
                             double *latency_seconds_out)
{
    // add latency estimate for failover delay if any IROB wasn't ACKed
    // 1) Figure out the earliest unACKed IROB
    // 2) Calculate the difference between now and the time it was sent
    // 3) That's the failover latency.  Add it to the estimates.

    PthreadScopedRWLock lock(&my_lock, true);

    struct timeval max_delay = {0, 0};
    bool failure = false;
    
    ostringstream s;
    for (irob_measurements_t::iterator it = irob_measurements.begin();
         it != irob_measurements.end(); ++it) {
        irob_id_t id = it->first;
        IROBMeasurement& measurement = it->second;
        measurement.mark_failed();
        s << id << " ";
        try {
            struct timeval delay = measurement.RTT();
            if (timercmp(&max_delay, &delay, <)) {
                max_delay = delay;
                failure = true;
            }
        } catch (InvalidEstimateException& e) {
            // ignore; not adding
        }
    }
    irob_measurements.clear();
    
    if (failure) {
        dbgprintf("Marking IROBs failed: [ %s]\n", s.str().c_str());
        double latency_ms = convert_to_useconds(max_delay) / 1000.0 / 2.0;
        double latency_seconds = latency_ms / 1000.0;

        bool record_failover_latency = true;
#ifndef CMM_UNIT_TESTING
        record_failover_latency = Config::getInstance()->getRecordFailoverLatency();
#endif
        if (record_failover_latency) {
            dbgprintf("Adding max failover delay as latency: %.6f seconds\n",
                      latency_seconds);
            
            FlipFlopEstimate& latency_estimate = net_estimates.estimates[NET_STATS_LATENCY];
            latency_estimate.add_observation(round_nearest(latency_ms));
            u_long new_latency_est;
            if (latency_estimate.get_estimate(new_latency_est)) {
#ifndef CMM_UNIT_TESTING
                chooser->reportNetStats(network_type, 0.0, 0.0, 
                                        latency_seconds, new_latency_est / 1000.0);
#endif
            }
        } else {
            dbgprintf("Observed max failover delay of %.6f seconds, "
                      "but not recording it as latency\n",
                      latency_seconds);
        }
        if (latency_seconds_out) {
            *latency_seconds_out = latency_seconds;
        }
        return true;
    }
    return false;
}

void
NetStats::remove(irob_id_t irob_id)
{
    irob_measurements.erase(irob_id);
}

IROBMeasurement::IROBMeasurement()
    : id(-1), finished(false)
{
    total_size = 0;
    //NetStats::get_time(arrival_time);
    arrival_time.tv_sec = arrival_time.tv_usec = -1;
    last_activity = arrival_time;
    ack_time.tv_sec = -1;
    ack_time.tv_usec = 0;
    failure_time = ack_time;
    total_delay.tv_sec = 0;
    total_delay.tv_usec = 0;
}

void 
IROBMeasurement::set_id(irob_id_t id_)
{
    id = id_;
}


size_t
IROBMeasurement::num_bytes()
{
    return total_size;
}

void 
IROBMeasurement::add_bytes(size_t bytes, struct timeval queuable_time,
                           u_long bw_est)
{
    struct timeval diff, now;
    NetStats::get_time(now);

    if (arrival_time.tv_sec == -1) {
        arrival_time = now;
    } else {
        // the time at which the message was ready to be sent
        //   (max of the last activity and the time at which
        //    this message could hit the network)
        queuable_time = (timercmp(&last_activity, &queuable_time, <)
                         ? queuable_time : last_activity);
        // by checking this time, we make sure not to double-count
        // scheduling delay that overlaps queuing delay
        
        struct timeval bw_time = time_to_send(total_size, bw_est);
        struct timeval self_queuable_time;
        timeradd(&arrival_time, &bw_time, &self_queuable_time);
        queuable_time = (timercmp(&queuable_time, &self_queuable_time, >)
                         ? queuable_time : self_queuable_time);

        if (timercmp(&queuable_time, &now, <)) {
            TIMEDIFF(queuable_time, now, diff);
            // add scheduling delay
            dbgprintf("Adding %lu.%06lu of scheduling delay to IROB %ld\n"
                      "(Actually ignoring it; see %s:%d)\n",
                      diff.tv_sec, diff.tv_usec, id, 
                      __FILE__, __LINE__);
            // In practice, this is either so tiny as to be negligible,
            //   or else absurdly large and seemingly bogus.
            // So we'll assume it's negligible.
            //add_delay(diff);
        }
    }
    last_activity = now;
    
    total_size += bytes;
}

void 
IROBMeasurement::add_delay(struct timeval delay)
{
    timeradd(&total_delay, &delay, &total_delay);
}

struct timeval
IROBMeasurement::RTT()
{
    ASSERT(ack_time.tv_sec != -1 || failure_time.tv_sec != -1);
    ASSERT(ack_time.tv_sec == -1 || failure_time.tv_sec == -1);

    struct timeval finish_time = (ack_time.tv_sec != -1 ? ack_time : failure_time);
    ASSERT(timercmp(&arrival_time, &finish_time, <=));

    struct timeval rtt;
    TIMEDIFF(arrival_time, finish_time, rtt);

    if (timercmp(&rtt, &total_delay, <=)) {
        throw InvalidEstimateException();
    }

    timersub(&rtt, &total_delay, &rtt);
    return rtt;
}

void
IROBMeasurement::ack(struct timeval *real_time)
{
    if (real_time) {
        ack_time = *real_time;
    } else {
        NetStats::get_time(ack_time);
    }
}

void
IROBMeasurement::mark_failed()
{
    NetStats::get_time(failure_time);
}


QueuingDelay::QueuingDelay()
{
    last_msg_time.tv_sec = last_msg_time.tv_usec = 0;
    last_msg_qdelay.tv_sec = -1;
    last_msg_qdelay.tv_usec = 0;
    last_msg_size = 0;
    last_bw_estimate = 0;
    last_irob = -1;
}

struct timeval 
QueuingDelay::get_queuable_time(irob_id_t irob_id) const
{
    if (irob_id != last_irob) {
        struct timeval inval = {0, 0};
        return inval;
    }

    struct timeval queuable_time = last_msg_time;
    timeradd(&queuable_time, &last_msg_qdelay, &queuable_time);
    
    struct timeval bw_time = time_to_send(last_msg_size, last_bw_estimate);
    timeradd(&queuable_time, &bw_time, &queuable_time);
    return queuable_time;
}

/* qdelay(t) = 0 if t == 0 (the first message is the
 *                          first in the queue)
 * qdelay(t+1) = max{ qdelay(t) + size(t)/bandwidth(t)
 *                    - (msg_time(t+1) - msg_time(t)),
 *                    0 }
 * (see paper)
 */
struct timeval 
QueuingDelay::add_message(size_t msg_size, u_long bw_estimate, 
                          irob_id_t irob_id)
{
    struct timeval zero = {0, 0};
    struct timeval cur_msg_time;
    NetStats::get_time(cur_msg_time);

    if (last_msg_qdelay.tv_sec == -1) {
        // base case; first message has no qdelay
        last_msg_time = cur_msg_time;
        last_msg_qdelay.tv_sec = 0;
        last_msg_size = msg_size;
        last_bw_estimate = bw_estimate;
        last_irob = irob_id;
        return last_msg_qdelay;
    }

    if (bw_estimate == 0) {
        // invalid bandwidth estimate; ignore queuing delay 
        // for this message
        return zero;
    }

    if (irob_id == last_irob && irob_id != -1) {
        dbgprintf("Same IROB as last time; adding to size\n");
        last_msg_size += msg_size;
        last_bw_estimate = bw_estimate;

        return zero;
    } else {
        // size(t)/bandwidth(t)
        struct timeval prev_bw_time = time_to_send(last_msg_size,
                                                   last_bw_estimate);
        
        struct timeval qdelay_calc = {0, 0};
        // qdelay(t) + size(t)/bandwidth(t)
        timeradd(&last_msg_qdelay, &prev_bw_time, &qdelay_calc);

        // msg_time(t+1) - msg_time(t)
        struct timeval diff = {0,0};
        if (timercmp(&last_msg_time, &cur_msg_time, <)) {
            TIMEDIFF(last_msg_time, cur_msg_time, diff);
        }
        
        if (timercmp(&qdelay_calc, &diff, >)) {
            // qdelay(t) + size(t)/bandwidth(t) - (msg_time(t+1) - msg_time(t))
            timersub(&qdelay_calc, &diff, &qdelay_calc);
            // now, qdelay_calc == qdelay(t+1)
            last_msg_qdelay = qdelay_calc;
        } else {
            last_msg_qdelay.tv_sec = 0;
            last_msg_qdelay.tv_usec = 0;
        }
        last_msg_time = cur_msg_time;
        last_msg_size = msg_size;
        last_irob = irob_id;
        last_bw_estimate = bw_estimate;

        return last_msg_qdelay;
    }
}



NetStats::time_getter_fn_t NetStats::time_getter = 
    (NetStats::time_getter_fn_t) &gettimeofday;

void
NetStats::set_time_getter(time_getter_fn_t new_gettimeofday)
{
    time_getter = new_gettimeofday;
}

void
NetStats::get_time(struct timeval& tv)
{
    time_getter(&tv, NULL);
}


IROBTransfersPerNetwork::IROBTransfersPerNetwork()
    : total_bytes(0)
{
}

size_t&
IROBTransfersPerNetwork::getValueRef(struct in_addr local_addr,
                                     struct in_addr remote_addr)
{
    pair<in_addr_t, in_addr_t> key = make_pair(local_addr.s_addr, remote_addr.s_addr);
    if (transfers_by_network.count(key) == 0) {
        transfers_by_network[key] = 0;
    }
    return transfers_by_network[key];
}

void 
IROBTransfersPerNetwork::addTransfer(struct in_addr local_addr,
                                     struct in_addr remote_addr, 
                                     size_t bytes)
{
    getValueRef(local_addr, remote_addr) += bytes;
}

bool 
IROBTransfersPerNetwork::hasAllBytes(struct in_addr local_addr,
                                     struct in_addr remote_addr)
{
    // because this doesn't get checked until post-ACK,
    // and ACK doesn't happen before I've sent any bytes
    ASSERT(total_bytes > 0);
    return (getValueRef(local_addr, remote_addr) >= total_bytes);
}

void 
IROBTransfersPerNetwork::reportTotalBytes(size_t total_bytes_)
{
    if (total_bytes == 0) {
        total_bytes = total_bytes_;
    }

    // it should never change after the first assignment, though
    //  it may be assigned again
    ASSERT(total_bytes == total_bytes_);
}

// return true iff there are no more transfers in this map.
bool
IROBTransfersPerNetwork::remove(struct in_addr local_addr,
                                struct in_addr remote_addr)
{
    pair<in_addr_t, in_addr_t> key = make_pair(local_addr.s_addr, remote_addr.s_addr);
    transfers_by_network.erase(key);
    return transfers_by_network.empty();
}


void 
NetStats::IROBTransfers::addTransfer(irob_id_t id, 
                                     struct in_addr local_addr,
                                     struct in_addr remote_addr,
                                     size_t bytes)
{
    irob_iface_transfers[id].addTransfer(local_addr, remote_addr, bytes);
}

bool 
NetStats::IROBTransfers::hasAllBytes(irob_id_t id,
                                     struct in_addr local_addr,
                                     struct in_addr remote_addr)
{
    return irob_iface_transfers[id].hasAllBytes(local_addr, remote_addr);
}

void 
NetStats::IROBTransfers::reportTotalBytes(irob_id_t id, size_t total_bytes)
{
    irob_iface_transfers[id].reportTotalBytes(total_bytes);
}

void
NetStats::IROBTransfers::removeAll(struct in_addr local_addr,
                                   struct in_addr remote_addr)
{
    for (IROBIfaceMap::iterator it = irob_iface_transfers.begin();
         it != irob_iface_transfers.end(); ) {
        if (it->second.remove(local_addr, remote_addr)) {
            // erase it if it is empty.
            irob_iface_transfers.erase(it++);
        } else {
            ++it;
        }
    }
}
