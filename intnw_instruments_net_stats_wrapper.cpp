#include "intnw_net_stats_wrapper.h"
#include "intnw_instruments_net_stats_wrapper.h"

InstrumentsWrappedNetStats::InstrumentsWrappedNetStats(double bw_up, double RTT_seconds)
{
    stats = new WrappedNetStats(bw_up, RTT_seconds);
    bw_up_estimator = create_external_estimator();
    rtt_estimator = create_external_estimator();

    update(bw_up, RTT_seconds);
}

InstrumentsWrappedNetStats::~InstrumentsWrappedNetStats()
{
    delete stats;
    free_external_estimator(bw_up_estimator);
    free_external_estimator(rtt_estimator);
}

void InstrumentsWrappedNetStats::report_upload(int datalen, double start, double duration)
{
    double bw_observation, latency_seconds_observation;
    stats->report_upload(datalen, start, duration,
                         &bw_observation, &latency_seconds_observation);
    double rtt_observation = latency_seconds_observation * 2.0;

    add_observation(bw_up_estimator, bw_observation, stats->get_bandwidth_up());
    add_observation(rtt_estimator, rtt_observation, stats->get_latency() * 2.0);
}

double InstrumentsWrappedNetStats::get_bandwidth_up(instruments_context_t ctx)
{
    return get_estimator_value(ctx, bw_up_estimator);
}

double InstrumentsWrappedNetStats::get_rtt(instruments_context_t ctx)
{
    return get_estimator_value(ctx, rtt_estimator);
}

void InstrumentsWrappedNetStats::update(double bw_up, double RTT_seconds)
{
    if (bw_up == 0) {
        // ignore; not a real measurement
        return;
    }
    
    stats->update(bw_up, RTT_seconds);

    add_observation(bw_up_estimator, bw_up, stats->get_bandwidth_up());
    add_observation(rtt_estimator, RTT_seconds, stats->get_latency() * 2.0);
}

void InstrumentsWrappedNetStats::update()
{
    // just to make sure the instruments estimators have at least one observation.
    update(stats->get_bandwidth_up(), stats->get_latency() * 2.0);
}
