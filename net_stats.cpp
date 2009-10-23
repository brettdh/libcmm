#include "net_stats.h"
#include <pthread.h>
#include "pthread_util.h"
#include <netinet/in.h>
#include <sys/types.h>
#include "timeops.h"
#include <cmath>

NetStats::NetStats(struct in_addr local_addr_, 
                   struct in_addr remote_addr_)
    : local_addr(local_addr_), remote_addr(remote_addr_)
{
    pthread_rwlock_init(&my_lock, NULL);
    last_RTT.tv_sec = last_srv_time.tv_sec = -1;
    last_RTT.tv_usec = last_srv_time.tv_usec = 0;
    last_req_size = 0;
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
    // inserts if not already present
    IROBMeasurement& measurement = irob_measurements[irob_id];
    measurement.add_bytes(bytes);
 
    u_long bw_est = 0;
    if (net_estimates[NET_STATS_BW_UP].get_estimate(bw_est)) {
        struct timeval qdelay = outgoing_qdelay.add_message(bytes, bw_est);
        measurement.add_delay(qdelay);
    }
    // TODO: anything else?
}

void 
NetStats::report_send_event(size_t bytes)
{
    PthreadScopedRWLock lock(&my_lock, true);
    u_long bw_est = 0;
    if (net_estimates[NET_STATS_BW_UP].get_estimate(bw_est)) {
        (void)outgoing_qdelay.add_message(bytes, bw_est);
    }
}

void
NetStats::report_recv_event(size_t bytes)
{
    PthreadScopedRWLock lock(&my_lock, true);
    u_long bw_est = 0;
    if (net_estimates[NET_STATS_BW_DOWN].get_estimate(bw_est)) {
        (void)incoming_qdelay.add_message(bytes, bw_est);
    }
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
    
    dbgprintf("Reporting new ACK\n");

    struct timeval RTT = measurement.RTT();
    size_t req_size = measurement.num_bytes();

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

        size_t numerator = ((req_size > last_req_size)
                            ? (req_size - last_req_size)
                            : (last_req_size - req_size));
        struct timeval RTT_diff, srv_time_diff;
        struct timeval denominator;
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
        assert(timercmp(&RTT, &srv_time, >));
        timersub(&RTT, &srv_time, &diff);
        double latency = (convert_to_useconds(diff)/1000.0 - 
                          (req_size / bw * 1000.0)) / 2.0;

        u_long bw_est = static_cast<u_long>(bw);
        u_long latency_est = static_cast<u_long>(latency);
        net_estimates[NET_STATS_BW_UP].add_observation(bw_est);
        net_estimates[NET_STATS_LATENCY].add_observation(latency_est);

        bool ret = net_estimates[NET_STATS_BW_UP].get_estimate(bw_est);
        ret = ret && net_estimates[NET_STATS_LATENCY].get_estimate(latency_est);
        assert(ret);
        dbgprintf("New estimates: bw_up %lu bytes/sec, latency %lu ms\n", 
                  bw_est, latency_est);
        // TODO: send bw_up estimate to remote peer as its bw_down.  Or maybe do that
        //       in CSocketReceiver, after calling this.
    }

    last_RTT = RTT;
    last_req_size = req_size;
    last_srv_time = srv_time;
}

IROBMeasurement::IROBMeasurement()
{
    total_size = 0;
    TIME(arrival_time);
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
IROBMeasurement::add_bytes(size_t bytes)
{
    struct timeval diff, now;
    TIME(now);
    TIMEDIFF(last_activity, now, diff);

    // add scheduling delay
    timeradd(&total_delay, &diff, &total_delay);
    
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
}


/* qdelay(t) = 0 if t == 0 (the first message is the
 *                          first in the queue)
 * qdelay(t+1) = max{ qdelay(t) + size(t)/bandwidth(t)
 *                    - (msg_time(t+1) - msg_time(t)),
 *                    0 }
 * (see paper)
 */
struct timeval 
QueuingDelay::add_message(size_t msg_size, u_long bw_estimate)
{
    if (bw_estimate == 0) {
        // invalid bandwidth estimate; ignore queuing delay 
        // for this message
        struct timeval zero = {0, 0};
        return zero;
    }

    struct timeval cur_msg_time;
    TIME(cur_msg_time);

    if (last_msg_qdelay.tv_sec == -1) {
        // base case; first message has no qdelay
        last_msg_time = cur_msg_time;
        last_msg_qdelay.tv_sec = 0;
        last_msg_size = msg_size;
        last_bw_estimate = bw_estimate;
        return last_msg_qdelay;
    }

    // size(t)/bandwidth(t)
    double prev_bw_seconds = ((double)last_msg_size) / last_bw_estimate;
    useconds_t prev_bw_time_usecs = (useconds_t)(prev_bw_seconds * 1000000);
    struct timeval prev_bw_time = convert_to_timeval(prev_bw_time_usecs);
    
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
    last_bw_estimate = bw_estimate;

    return last_msg_qdelay;
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
    est = static_cast<u_long>(ret);
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
