#include "intnw_net_stats_wrapper.h"
#include "intnw_instruments_net_stats_wrapper.h"
#include "debug.h"
#include "config.h"

#include <instruments_private.h>

#include <assert.h>

#include <string>
#include <map>
using std::string; using std::map;

static EstimatorRangeHints 
get_range_hints(const string& network, const string& type)
{
    map<string, map<string, decltype(&Config::getCellularRttRangeHints)> > getters = {
        { "wifi", {
                { "bandwidth", &Config::getWifiBandwidthRangeHints },
                { "RTT", &Config::getWifiRttRangeHints },
                { "session-duration", &Config::getWifiSessionDurationRangeHints }
            },
        },
        { "cellular", {
                { "bandwidth", &Config::getCellularBandwidthRangeHints },
                { "RTT", &Config::getCellularRttRangeHints },
                { "session-duration", &Config::getCellularSessionDurationRangeHints }
            }
          
        }
    };
    assert(getters.count(network) > 0 &&
           getters[network].count(type) > 0);
    auto getter = getters[network][type];
    Config *config = Config::getInstance();
    return (config->*getter)();
}

InstrumentsWrappedNetStats::InstrumentsWrappedNetStats(const std::string& network)
    : session_duration(network + "_session_duration"), first_update(true)
{
    dbgprintf("creating InstrumentsWrappedNetStats %p\n", this);
    
    bw_up_estimator = create_external_estimator((network + "-bandwidth").c_str());
    rtt_estimator = create_external_estimator((network + "-RTT").c_str());
    
    double shape, scale;
    use_session_distribution = Config::getInstance()->getWifiSessionLengthDistributionParams(shape, scale);
    if (use_session_distribution) {
        session_length_distribution = create_continuous_distribution(shape, scale);
    } else {
        session_duration_estimator = create_external_estimator((network + "-session-duration").c_str());
    }
    
    EstimatorRangeHints bw_hints = get_range_hints(network, "bandwidth");
    EstimatorRangeHints rtt_hints = get_range_hints(network, "RTT");
    EstimatorRangeHints session_duration_hints = get_range_hints(network, "session-duration");
    set_estimator_range_hints(bw_up_estimator, bw_hints.min, bw_hints.max, bw_hints.num_bins);
    set_estimator_range_hints(rtt_estimator, rtt_hints.min, rtt_hints.max, rtt_hints.num_bins);

    if (!use_session_distribution) {
        set_estimator_range_hints(session_duration_estimator, 
                                  session_duration_hints.min, 
                                  session_duration_hints.max, 
                                  session_duration_hints.num_bins);
    }
}

InstrumentsWrappedNetStats::~InstrumentsWrappedNetStats()
{
    dbgprintf("destroying InstrumentsWrappedNetStats %p\n", this);

    free_external_estimator(bw_up_estimator);
    free_external_estimator(rtt_estimator);
    if (use_session_distribution) {
        free_continuous_distribution(session_length_distribution);
    } else {
        free_external_estimator(session_duration_estimator);
    }
}

double InstrumentsWrappedNetStats::get_bandwidth_up(instruments_context_t ctx)
{
    return get_estimator_value(ctx, bw_up_estimator);
}

double InstrumentsWrappedNetStats::get_rtt(instruments_context_t ctx)
{
    return get_estimator_value(ctx, rtt_estimator);
}

double 
InstrumentsWrappedNetStats::get_session_duration(instruments_context_t ctx)
{
    return get_estimator_value(ctx, session_duration_estimator);
}

double
InstrumentsWrappedNetStats::getWifiFailurePenalty(instruments_context_t ctx, 
                                                  double transfer_time, 
                                                  double current_wifi_duration,
                                                  double wifi_failover_window)
{
    // when detecting failure, use the entire failover window, since
    //   some delay will be experienced if the window intersects the transfer.
    // when calculating the penalty, though, 
    //   divide wifi failover cost by 2, which will on average be
    //   the amount of failover delay actually experienced by 
    //   a failed wifi transfer.
    // XXX: this should actually be calculated as the integral 
    // XXX: over the failover window, but this is a decent approximation
    // XXX: and much less complicated.

    double penalty = wifi_failover_window / 2.0;
    if (use_session_distribution) {
        double failure_window_end = current_wifi_duration + transfer_time + wifi_failover_window;
        double fail_prob = get_probability_value_is_in_range(session_length_distribution,
                                                             current_wifi_duration, 
                                                             failure_window_end);
        return penalty * fail_prob;
    } else {
        double predicted_wifi_duration = get_session_duration(ctx);
        if (current_wifi_duration >= (predicted_wifi_duration - wifi_failover_window)) {
            // must offset by failover delay, since I don't know
            // whether I'm currently in a failover period
            return penalty;
        }
        return 0.0;
    }
}

void InstrumentsWrappedNetStats::update(double bw_up, double bw_estimate,
                                        double RTT_seconds, double RTT_estimate)
{
    if (bw_up == 0 && RTT_seconds == 0) {
        // ignore; not a real measurement
        return;
    }

    // XXX: hackish.  Should separate bw and RTT updates.
    do {
        if (bw_up > 0.0) {
            add_observation(bw_up_estimator, bw_up, bw_estimate);
        }
        if (RTT_seconds > 0.0) {
            add_observation(rtt_estimator, RTT_seconds, RTT_estimate);
        }
    } while (was_first_update());
}

bool
InstrumentsWrappedNetStats::was_first_update()
{
    bool ret = first_update;
    first_update = false;
    return ret;
}

void 
InstrumentsWrappedNetStats::addSessionDuration(struct timeval duration)
{
    double duration_secs = duration.tv_sec + (duration.tv_usec / 1000000.0);
    double duration_est;
    session_duration.add_observation(duration_secs);
    if (session_duration.get_estimate(duration_est)) {
        dbgprintf("Adding new session length %f  new estimate %f\n",
                  duration_secs, duration_est);
        if (!use_session_distribution) {
            add_observation(session_duration_estimator, duration_secs, duration_est);
        }
    }
}

void 
InstrumentsWrappedNetStats::loadSessionLength(std::istream& in)
{
    session_duration.load(in);
    double duration_est;
    bool success = session_duration.get_estimate(duration_est);
    assert(success); // if we are loading it, there must be a valid estimate.

    if (!use_session_distribution) {
        // add initial value to instruments estimator
        add_observation(session_duration_estimator, duration_est, duration_est);
    }
}

void InstrumentsWrappedNetStats::saveSessionLength(std::ostream& out)
{
    session_duration.save(out);
}


void InstrumentsWrappedNetStats::setWifiSessionLengthBound(double cur_session_length)
{
    if (!use_session_distribution) {
        set_estimator_condition(session_duration_estimator, 
                                INSTRUMENTS_ESTIMATOR_VALUE_AT_LEAST, 
                                cur_session_length);
    }
}
