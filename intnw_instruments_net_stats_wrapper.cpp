#include "intnw_net_stats_wrapper.h"
#include "intnw_instruments_net_stats_wrapper.h"
#include "intnw_instruments_network_chooser.h"
#include "debug.h"
#include "intnw_config.h"

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
    ASSERT(getters.count(network) > 0 &&
           getters[network].count(type) > 0);
    auto getter = getters[network][type];
    Config *config = Config::getInstance();
    return (config->*getter)();
}

InstrumentsWrappedNetStats::InstrumentsWrappedNetStats(const std::string& network,
                                                       IntNWInstrumentsNetworkChooser *chooser_)
    : chooser(chooser_), session_duration(network + "_session_duration"), first_update(true),
      last_bw_estimate(0.0), last_RTT_estimate(0.0), // will be filled in before use
      first_bw_up_estimate(0.0), first_RTT_estimate(0.0)
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
                                                  double penalty)
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

    double wifi_failover_window = Config::getInstance()->getWifiFailoverDelay();
    if (use_session_distribution) {
        double failure_window_end = current_wifi_duration + transfer_time + wifi_failover_window;
        double fail_prob = get_probability_value_is_in_range(session_length_distribution,
                                                             current_wifi_duration, 
                                                             failure_window_end);
        return penalty * fail_prob;
    } else {
        if (ctx) { // so I can have a harmless call if for some reason I turn off the weibull distribution
            double predicted_wifi_duration = get_session_duration(ctx);
            if (current_wifi_duration >= (predicted_wifi_duration - wifi_failover_window)) {
                // must offset by failover delay, since I don't know
                // whether I'm currently in a failover period
                return penalty;
            }
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
            // The prob-bounds method gets wildly thrown off by erratic, infrequent bandwidth measurments.
            // Furthermore, bandwidth hardly makes any difference in the network decisions for small transfers.
            // So, we'll just ignore it for that method.
            // (Could ignore it for the others too, but it doesn't seem to hurt them.)
            // XXX: this is a gross hack, and it's actually disabled via config.
            // XXX: bandwidth error is always considered in my experiments.
            EvalMethod method = Config::getInstance()->getEstimatorErrorEvalMethod();
            if (Config::getInstance()->getDisableBandwidthError() == false ||
                (method != CONFIDENCE_BOUNDS && method != CONFIDENCE_BOUNDS_WEIGHTED)) {
                chooser->unlock();
                // this grabs a lock on each subscriber to this estimator,
                // which can result in deadlock, so drop the eval lock first.
                add_observation(bw_up_estimator, bw_up, bw_estimate);
                chooser->lock();
                if (first_update) {
                    first_bw_up_estimate = bw_estimate;
                }
            }
            last_bw_estimate = bw_estimate;
        }
        if (RTT_seconds > 0.0) {
            chooser->unlock(); // see above.
            add_observation(rtt_estimator, RTT_seconds, RTT_estimate);
            chooser->lock();

            if (first_update) {
                first_RTT_estimate = RTT_estimate;
            }
            last_RTT_estimate = RTT_estimate;
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
            chooser->unlock(); // see comments in update()
            add_observation(session_duration_estimator, duration_secs, duration_est);
            chooser->lock();
        }
    }
}

void 
InstrumentsWrappedNetStats::loadSessionLength(std::istream& in)
{
    session_duration.load(in);
    double duration_est;
    bool success = session_duration.get_estimate(duration_est);
    ASSERT(success); // if we are loading it, there must be a valid estimate.

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

void
InstrumentsWrappedNetStats::setRttLowerBound(double min_rtt)
{
    set_estimator_condition(rtt_estimator, 
                            INSTRUMENTS_ESTIMATOR_VALUE_AT_LEAST,
                            min_rtt);
}

void
InstrumentsWrappedNetStats::clearRttLowerBound()
{
    clear_estimator_conditions(rtt_estimator);
}

void
InstrumentsWrappedNetStats::resetError()
{
    // the stats here are the only ones that matter to the strategies that use instruments.
    // the other strategies don't care about resetting, so we don't do it on the cached NetStats.

    // reset these first, so that the historical error starts with the historical estimate
    add_observation(bw_up_estimator, first_bw_up_estimate, first_bw_up_estimate);
    add_observation(rtt_estimator, first_RTT_estimate, first_RTT_estimate);

    reset_estimator_error(bw_up_estimator);
    reset_estimator_error(rtt_estimator);
}
