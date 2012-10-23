#ifndef _INTNW_INSTRUMENTS_NET_STATS_WRAPPER_H_
#define _INTNW_INSTRUMENTS_NET_STATS_WRAPPER_H_

#include <libcmm_irob.h>
#include <net_interface.h>

#include <instruments.h>

class WrappedNetStats;

class InstrumentsWrappedNetStats {
  public:
    InstrumentsWrappedNetStats();
    ~InstrumentsWrappedNetStats();

    // add active measurement result to stats.
    void update(double bw_up, double bw_estimate,
                double RTT_seconds, double RTT_estimate);

    double get_bandwidth_up(instruments_context_t ctx);
    double get_rtt(instruments_context_t ctx);
  private:
    instruments_external_estimator_t bw_up_estimator;
    instruments_external_estimator_t rtt_estimator;
};

#endif /* _INTNW_INSTRUMENTS_NET_STATS_WRAPPER_H_ */
