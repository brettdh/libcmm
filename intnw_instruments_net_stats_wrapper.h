#ifndef _INTNW_INSTRUMENTS_NET_STATS_WRAPPER_H_
#define _INTNW_INSTRUMENTS_NET_STATS_WRAPPER_H_

#include <libcmm_irob.h>
#include <net_interface.h>
#include "estimate.h"

#include <instruments.h>

#include <string>
#include <iostream>

class WrappedNetStats;

class InstrumentsWrappedNetStats {
  public:
    InstrumentsWrappedNetStats(const std::string& network);
    ~InstrumentsWrappedNetStats();

    // add active measurement result to stats.
    void update(double bw_up, double bw_estimate,
                double RTT_seconds, double RTT_estimate);

    // for keeping track of wifi session durations.
    void addSessionDuration(struct timeval duration);

    // used to tell instruments the lower bound on 
    // current wifi session length.
    void setWifiSessionLengthBound(double cur_session_length);

    void loadSessionLength(std::istream& in);
    void saveSessionLength(std::ostream& out);

    double get_bandwidth_up(instruments_context_t ctx);
    double get_rtt(instruments_context_t ctx);
    double get_session_duration(instruments_context_t ctx);
  private:
    instruments_external_estimator_t bw_up_estimator;
    instruments_external_estimator_t rtt_estimator;

    Estimate session_duration;
    instruments_external_estimator_t session_duration_estimator;

    // used to do a "double-update" on the first real update,
    //  so that I get an error value in the distribution.
    bool first_update;
    bool was_first_update();
};

#endif /* _INTNW_INSTRUMENTS_NET_STATS_WRAPPER_H_ */
