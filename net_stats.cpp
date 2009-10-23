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
    estimates_valid = false;
}

bool 
NetStats::get_estimate(unsigned short type, u_long *value)
{
    if (type >= NUM_ESTIMATES || value == NULL) {
        return false;
    }

    PthreadScopedRWLock lock(&my_lock, false);

    if (!estimates_valid) {
        // not enough data yet
        return false;
    }

    *value = net_estimates[type].get_estimate();
    return true;
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
NetStats::add_queuing_delay(irob_id_t irob_id, struct timeval delay)
{
    
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

u_long
Estimate::get_estimate()
{
    double ret;
    if (spot_value_within_limits()) {
        ret = agile_estimate;
    } else {
        ret = stable_estimate;
    }
    return static_cast<u_long>(ret);
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
