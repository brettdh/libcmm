#include "estimate.h"

#include <sys/types.h>
#include "timeops.h"
#include <cmath>

u_long round_nearest(double val)
{
    return static_cast<u_long>(val + 0.5);
}

Estimate::Estimate()
    : stable_estimate(0.0), agile_estimate(0.0), spot_value(0.0),
      moving_range(0.0), center_line(0.0), valid(false)
{
}

void
Estimate::reset(u_long new_spot_value_int)
{
    stable_estimate = agile_estimate = spot_value = 0.0;
    moving_range = center_line = 0.0;
    valid = false;
    add_observation(new_spot_value_int);
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
