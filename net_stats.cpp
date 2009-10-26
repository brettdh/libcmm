#include "net_stats.h"
#include <pthread.h>
#include "pthread_util.h"
#include <netinet/in.h>
#include <sys/types.h>
#include "timeops.h"
#include <cmath>

NetStats::NetStats(struct net_interface local_iface, 
                   struct net_interface remote_iface)
    : local_addr(local_iface.ip_addr), remote_addr(remote_iface.ip_addr)
{
    pthread_rwlock_init(&my_lock, NULL);
    last_RTT.tv_sec = last_srv_time.tv_sec = -1;
    last_RTT.tv_usec = last_srv_time.tv_usec = 0;
    last_req_size = 0;

    u_long init_bandwidth = iface_bandwidth(local_iface, remote_iface);
    u_long init_latency = iface_RTT(local_iface, remote_iface) / 2;
    if (init_bandwidth > 0) {
        net_estimates[NET_STATS_BW_UP].add_observation(init_bandwidth);
    }
    if (init_latency > 0) {
        net_estimates[NET_STATS_LATENCY].add_observation(init_latency);
    }
}

bool 
NetStats::get_estimate(unsigned short type, u_long& value)
{
    if (type >= NUM_ESTIMATES) {
        return false;
    }

    PthreadScopedRWLock lock(&my_lock, false);

    return net_estimates[type].get_estimate(value);
}

void 
NetStats::report_send_event(irob_id_t irob_id, size_t bytes)
{
    PthreadScopedRWLock lock(&my_lock, true);

    u_long bw_est = 0;
    (void)net_estimates[NET_STATS_BW_UP].get_estimate(bw_est);
    
    struct timeval queuable_time = outgoing_qdelay.get_queuable_time(irob_id);
    // queuable_time is the earliest time that this message 
    //  could hit the network (or else it's {0, 0}, if
    //  the last message was from a different IROB; that is,
    //  if the message could go immediately)

    // inserts if not already present
    IROBMeasurement& measurement = irob_measurements[irob_id];
    measurement.add_bytes(bytes, queuable_time); // accounts for inter-send delay

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
NetStats::report_send_event(size_t bytes)
{
    PthreadScopedRWLock lock(&my_lock, true);
    u_long bw_est = 0;
    (void)net_estimates[NET_STATS_BW_UP].get_estimate(bw_est);
    (void)outgoing_qdelay.add_message(bytes, bw_est);
}

void
NetStats::report_recv_event(size_t bytes)
{
    PthreadScopedRWLock lock(&my_lock, true);
    u_long bw_est = 0;
    (void)net_estimates[NET_STATS_BW_DOWN].get_estimate(bw_est);
    (void)incoming_qdelay.add_message(bytes, bw_est);
}

static u_long round_nearest(double val)
{
    return static_cast<u_long>(val + 0.5);
}

void 
NetStats::report_ack(irob_id_t irob_id, struct timeval srv_time)
{
    PthreadScopedRWLock lock(&my_lock, true);
    assert(irob_measurements.find(irob_id) !=
           irob_measurements.end());
    IROBMeasurement measurement = irob_measurements[irob_id];
    irob_measurements.erase(irob_id);

    measurement.ack();
    
    struct timeval RTT = measurement.RTT();
    size_t req_size = measurement.num_bytes();

    dbgprintf("Reporting new ACK for IROB %ld; RTT %lu.%06lu  size %zu\n",
              irob_id, RTT.tv_sec, RTT.tv_usec, req_size);

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
        bool valid_result = false;

        if (req_size != last_req_size) {
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
            } else assert(0);

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

            bw_est = round_nearest(bw);
            latency_est = round_nearest(latency);
            valid_result = (bw > 0 && latency > 0);
            if (!valid_result) {
                dbgprintf("Spot values indicate invalid observation; ignoring\n");
            }
        } else if(net_estimates[NET_STATS_BW_UP].get_estimate(bw_est)) {
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
            latency_est = round_nearest(latency);
            valid_result = (bw > 0 && latency > 0);
            if (!valid_result) {
                dbgprintf("Spot values indicate invalid observation; ignoring\n");
            }
        } else {
            dbgprintf("Couldn't produce spot values; equal message "
                      "sizes and no prior bw estimate\n");
        }

        if (valid_result) {
            net_estimates[NET_STATS_BW_UP].add_observation(bw_est);
            net_estimates[NET_STATS_LATENCY].add_observation(latency_est);
            dbgprintf("New spot values: bw %lu latency %lu\n", bw_est, latency_est);
            
            bool ret = net_estimates[NET_STATS_BW_UP].get_estimate(bw_est);
            ret = ret && net_estimates[NET_STATS_LATENCY].get_estimate(latency_est);
            assert(ret);
            dbgprintf("New estimates: bw_up %lu bytes/sec, latency %lu ms\n", 
                      bw_est, latency_est);
            // TODO: send bw_up estimate to remote peer as its bw_down.  Or maybe do that
            //       in CSocketReceiver, after calling this.
        }
    }

    last_RTT = RTT;
    last_req_size = req_size;
    last_srv_time = srv_time;
}

void
NetStats::remove(irob_id_t irob_id)
{
    irob_measurements.erase(irob_id);
}

IROBMeasurement::IROBMeasurement()
{
    total_size = 0;
    //TIME(arrival_time);
    arrival_time.tv_sec = arrival_time.tv_usec = -1;
    last_activity = arrival_time;
    ack_time.tv_sec = -1;
    ack_time.tv_usec = 0;
    total_delay.tv_sec = 0;
    total_delay.tv_usec = 0;
}

size_t
IROBMeasurement::num_bytes()
{
    return total_size;
}

void 
IROBMeasurement::add_bytes(size_t bytes, struct timeval queuable_time)
{
    struct timeval diff, now;
    TIME(now);

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

        if (timercmp(&queuable_time, &now, <)) {
            TIMEDIFF(queuable_time, now, diff);
            // add scheduling delay
            dbgprintf("Adding %lu.%06lu of scheduling delay\n",
                      diff.tv_sec, diff.tv_usec);
            timeradd(&total_delay, &diff, &total_delay);
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
    assert(ack_time.tv_sec != -1);
    assert(timercmp(&arrival_time, &ack_time, <));

    struct timeval rtt;
    TIMEDIFF(arrival_time, ack_time, rtt);

    assert(timercmp(&rtt, &total_delay, >));

    timersub(&rtt, &total_delay, &rtt);
    return rtt;
}

void
IROBMeasurement::ack()
{
    TIME(ack_time);
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

static struct timeval time_to_send(size_t msg_size, u_long bw_estimate)
{
    double bw_seconds = ((double)msg_size) / bw_estimate;
    useconds_t bw_time_usecs = (useconds_t)(bw_seconds * 1000000);
    struct timeval bw_time = convert_to_timeval(bw_time_usecs);
    return bw_time;
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
    TIME(cur_msg_time);

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
        struct timeval diff;
        TIMEDIFF(last_msg_time, cur_msg_time, diff);
        
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

Estimate::Estimate()
    : stable_estimate(0.0), agile_estimate(0.0), spot_value(0.0),
      moving_range(0.0), center_line(0.0), valid(false)
{
}

bool
Estimate::get_estimate(u_long& est)
{
    // Estimate can be:
    //  * nothing if there have been no spot values (returns false)
    //  * the first spot value if there's only been one (returns true)
    //  * A real estimate based on two or more spot values (returns true)

    double ret;
    
    if (!valid) {
        return false;
    }

    if (spot_value_within_limits()) {
        ret = agile_estimate;
    } else {
        ret = stable_estimate;
    }
    est = round_nearest(ret);
    return true;
}

#define STABLE_GAIN 0.9
#define AGILE_GAIN  0.1

// based on Figure 4 from the paper, it looks like the
//  moving range estimate should be stable and the 
//  center line estimate should be agile.
#define MOVING_RANGE_GAIN 0.9
#define CENTER_LINE_GAIN 0.1

#define STDDEV_ESTIMATOR 1.128f

void update_EWMA(double& EWMA, double spot, double gain)
{
    EWMA = gain * EWMA + (1 - gain) * spot;
}

void
Estimate::add_observation(u_long new_spot_value_int)
{
    double new_spot_value = static_cast<double>(new_spot_value_int);
    double new_MR_value = fabs(new_spot_value - spot_value);
    spot_value = new_spot_value;

    if (!valid) {
        center_line = stable_estimate = agile_estimate = new_spot_value;
        // moving_range remains 0.0 until I have a second spot value

        valid = true;
        return;
    }

    update_EWMA(agile_estimate, spot_value, AGILE_GAIN);
    update_EWMA(stable_estimate, spot_value, STABLE_GAIN);
    
    if (spot_value_within_limits()) {
        update_EWMA(moving_range, new_MR_value, MOVING_RANGE_GAIN);
    }
    update_EWMA(center_line, new_spot_value, CENTER_LINE_GAIN);
}

bool
Estimate::spot_value_within_limits()
{
    double limit_distance = 3.0 * moving_range / STDDEV_ESTIMATOR;
    double lower = center_line - limit_distance;
    double upper = center_line + limit_distance;
    return (spot_value >= lower && spot_value <= upper);
}
