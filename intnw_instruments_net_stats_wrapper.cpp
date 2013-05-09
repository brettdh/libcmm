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
                { "RTT", &Config::getWifiRttRangeHints }
            },
        },
        { "cellular", {
                { "bandwidth", &Config::getCellularBandwidthRangeHints },
                { "RTT", &Config::getCellularRttRangeHints }
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
    : first_update(true) 
{
    dbgprintf("creating InstrumentsWrappedNetStats %p\n", this);
    
    bw_up_estimator = create_external_estimator((network + "-bandwidth").c_str());
    rtt_estimator = create_external_estimator((network + "-RTT").c_str());
    
    EstimatorRangeHints bw_hints = get_range_hints(network, "bandwidth");
    EstimatorRangeHints rtt_hints = get_range_hints(network, "RTT");
    set_estimator_range_hints(bw_up_estimator, bw_hints.min, bw_hints.max, bw_hints.num_bins);
    set_estimator_range_hints(rtt_estimator, rtt_hints.min, rtt_hints.max, rtt_hints.num_bins);
}

InstrumentsWrappedNetStats::~InstrumentsWrappedNetStats()
{
    dbgprintf("destroying InstrumentsWrappedNetStats %p\n", this);

    free_external_estimator(bw_up_estimator);
    free_external_estimator(rtt_estimator);
}

double InstrumentsWrappedNetStats::get_bandwidth_up(instruments_context_t ctx)
{
    return get_estimator_value(ctx, bw_up_estimator);
}

double InstrumentsWrappedNetStats::get_rtt(instruments_context_t ctx)
{
    return get_estimator_value(ctx, rtt_estimator);
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
