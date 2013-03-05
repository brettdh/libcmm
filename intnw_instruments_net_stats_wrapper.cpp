#include "intnw_net_stats_wrapper.h"
#include "intnw_instruments_net_stats_wrapper.h"
#include "debug.h"

InstrumentsWrappedNetStats::InstrumentsWrappedNetStats(const std::string& network)
    : first_update(true) 
{
    dbgprintf("creating InstrumentsWrappedNetStats %p\n", this);
    
    bw_up_estimator = create_external_estimator((network + "-bandwidth").c_str());
    rtt_estimator = create_external_estimator((network + "-RTT").c_str());
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
