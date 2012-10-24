#include "intnw_net_stats_wrapper.h"
#include "intnw_instruments_net_stats_wrapper.h"

InstrumentsWrappedNetStats::InstrumentsWrappedNetStats()
    : first_update(true) 
{
    bw_up_estimator = create_external_estimator();
    rtt_estimator = create_external_estimator();
}

InstrumentsWrappedNetStats::~InstrumentsWrappedNetStats()
{
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
    if (bw_up == 0) {
        // ignore; not a real measurement
        return;
    }

    do {
        add_observation(bw_up_estimator, bw_up, bw_estimate);
        add_observation(rtt_estimator, RTT_seconds, RTT_estimate);
    } while (was_first_update());
}

bool
InstrumentsWrappedNetStats::was_first_update()
{
    bool ret = first_update;
    first_update = false;
    return ret;
}
