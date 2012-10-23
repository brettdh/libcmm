#ifndef _INTNW_INSTRUMENTS_NET_STATS_WRAPPER_H_
#define _INTNW_INSTRUMENTS_NET_STATS_WRAPPER_H_

#include <libcmm_irob.h>
#include <net_interface.h>

#include <instruments.h>

class WrappedNetStats;

class InstrumentsWrappedNetStats {
  public:
    InstrumentsWrappedNetStats(double bw_up, double RTT_seconds);
    ~InstrumentsWrappedNetStats();
    void report_upload(int datalen, double start, double duration);

    // add active measurement result to stats.
    void update(double bw_up, double RTT_seconds);

    // re-add the last active measurement result to the stats,
    //  in order to make sure that the estimators have at least one observation.
    void update();

    double get_bandwidth_up(instruments_context_t ctx);
    double get_rtt(instruments_context_t ctx);
  private:
    WrappedNetStats *stats;
    
    instruments_external_estimator_t bw_up_estimator;
    instruments_external_estimator_t rtt_estimator;
};

#endif /* _INTNW_INSTRUMENTS_NET_STATS_WRAPPER_H_ */
