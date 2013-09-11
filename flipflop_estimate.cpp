#include "flipflop_estimate.h"
#include "debug.h"
using intnw::check;

#include <sys/types.h>
#include "timeops.h"
#include <cmath>
#include <string>
#include <iomanip>
using std::string; using std::setprecision;
using std::boolalpha; using std::endl;

u_long round_nearest(double val)
{
    return static_cast<u_long>(val + 0.5);
}

FlipFlopEstimate::FlipFlopEstimate(const string& name_)
    : name(name_)
{
    init();
}

void
FlipFlopEstimate::init()
{
    fields.assign({
        { "stable_estimate", &stable_estimate },
        { "agile_estimate", &agile_estimate },
        { "last_spot_value", &last_spot_value },
        { "moving_range", &moving_range },
        { "center_line", &center_line }
    });
    for (auto& f : fields) {
        *f.dest = 0.0;
    }
    out_of_control = false;
    valid = false;
}

FlipFlopEstimate::FlipFlopEstimate(const FlipFlopEstimate& other)
{
    init();
    *this = other;
}

FlipFlopEstimate& 
FlipFlopEstimate::operator=(const FlipFlopEstimate& other)
{
    ASSERT(fields.size() > 0);
    ASSERT(fields.size() == other.fields.size());
    for (size_t i = 0; i < fields.size(); ++i) {
        ASSERT(fields[i].name == other.fields[i].name);
        *fields[i].dest = *other.fields[i].dest;
    }
    out_of_control = other.out_of_control;
    valid = other.valid;
    name = other.name;
    return *this;
}

void
FlipFlopEstimate::reset(double new_spot_value)
{
    init();
    add_observation(new_spot_value);
}

bool
FlipFlopEstimate::get_estimate(double& est)
{
    // FlipFlopEstimate can be:
    //  * nothing if there have been no spot values (returns false)
    //  * the first spot value if there's only been one (returns true)
    //  * A real estimate based on two or more spot values (returns true)

    if (!valid) {
        return false;
    }

    if (out_of_control) {
        est = stable_estimate;
    } else {
        est = agile_estimate;
    }
    return true;
}

bool
FlipFlopEstimate::get_estimate(u_long& est)
{
    double float_est;
    bool ret = get_estimate(float_est);
        
    est = round_nearest(float_est);
    return ret;
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
FlipFlopEstimate::add_observation(double new_spot_value)
{
    double new_MR_value = fabs(new_spot_value - last_spot_value);
    last_spot_value = new_spot_value;

    if (!valid) {
        center_line = stable_estimate = agile_estimate = new_spot_value;
        // moving_range remains 0.0 until I have a second spot value

        valid = true;
    } else {
        update_EWMA(agile_estimate, new_spot_value, AGILE_GAIN);
        update_EWMA(stable_estimate, new_spot_value, STABLE_GAIN);
        
        out_of_control = !spot_value_within_limits(new_spot_value);
        if (moving_range == 0.0 || spot_value_within_limits(new_spot_value)) {
            update_EWMA(moving_range, new_MR_value, MOVING_RANGE_GAIN);
        }
        update_EWMA(center_line, new_spot_value, CENTER_LINE_GAIN);
    }
    dbg_print(new_spot_value);
}

void
FlipFlopEstimate::dbg_print(double new_spot_value)
{
    double flipflop_value = 0.0;
    bool success = get_estimate(flipflop_value);
    ASSERT(success);
#ifdef CMM_DEBUG
    double limit_distance = calc_limit_distance();
#endif
    dbgprintf("%s estimate: new_obs %f stable %f agile %f "
              "center_line %f moving_range %f "
              "control_limits [ %f %f ] flipflop_value %f\n",
              name.c_str(), new_spot_value, stable_estimate, agile_estimate, 
              center_line, moving_range, 
              center_line - limit_distance, center_line + limit_distance,
              flipflop_value);
}

double
FlipFlopEstimate::calc_limit_distance()
{
    return 3.0 * moving_range / STDDEV_ESTIMATOR;
}

bool
FlipFlopEstimate::spot_value_within_limits(double spot_value)
{
    double limit_distance = calc_limit_distance();
    double lower = center_line - limit_distance;
    double upper = center_line + limit_distance;
    return (spot_value >= lower && spot_value <= upper);
}

static const size_t PRECISION = 10;


void 
FlipFlopEstimate::save(std::ostream& out)
{
    out << setprecision(PRECISION);
    for (auto& f : fields) {
        out << f.name << " " << *f.dest << endl;
    }
    out << "out_of_control " << boolalpha << out_of_control << endl;
}

void 
FlipFlopEstimate::load(std::istream& in)
{
    string field_name;
    for (auto& f : fields) {
        check(in >> field_name, "Failed to read a field in network stats file");
        check(field_name == f.name, "Got unexpected field in network stats file");
        check(in >> *f.dest, "Failed to read field value");
    }
    check(in >> field_name, "Failed to read saved out_of_control value");
    check(field_name == "out_of_control", "Got unexpected field at end of network stats file");
    check(in >> boolalpha >> out_of_control, "Failed to read out_of_control bool value");
    
    valid = true;

    double dummy_spot_value = 0.0;
    bool success = get_estimate(dummy_spot_value);
    ASSERT(success);
    
    dbg_print(dummy_spot_value);
}
