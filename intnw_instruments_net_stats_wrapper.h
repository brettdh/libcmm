#ifndef _INTNW_INSTRUMENTS_NET_STATS_WRAPPER_H_
#define _INTNW_INSTRUMENTS_NET_STATS_WRAPPER_H_

#include <libcmm_irob.h>
#include <net_interface.h>
#include "flipflop_estimate.h"

#include <instruments.h>

#include <string>
#include <iostream>

class WrappedNetStats;
class IntNWInstrumentsNetworkChooser;

class InstrumentsWrappedNetStats {
  public:
    InstrumentsWrappedNetStats(const std::string& network,
                               IntNWInstrumentsNetworkChooser *chooser_);
    ~InstrumentsWrappedNetStats();

    // add active measurement result to stats.
    void update(double bw_up, double bw_estimate,
                double RTT_seconds, double RTT_estimate);

    // for keeping track of wifi session durations.
    void addSessionDuration(struct timeval duration);

    // used to tell instruments the lower bound on 
    // current wifi session length.
    void setWifiSessionLengthBound(double cur_session_length);

    // for use just before a reevaluation
    void setRttLowerBound(double min_rtt);
    
    // after the reevaluation is over
    void clearRttLowerBound();

    void loadSessionLength(std::istream& in);
    void saveSessionLength(std::ostream& out);

    double get_bandwidth_up(instruments_context_t ctx);
    double get_rtt(instruments_context_t ctx);
    double get_session_duration(instruments_context_t ctx);
    double getWifiFailurePenalty(instruments_context_t ctx, 
                                 double transfer_time, 
                                 double current_wifi_duration,
                                 double penalty);

    instruments_estimator_t getRttEstimator() {
        return rtt_estimator;
    }
  private:
    IntNWInstrumentsNetworkChooser *chooser;

    instruments_external_estimator_t bw_up_estimator;
    instruments_external_estimator_t rtt_estimator;

    FlipFlopEstimate session_duration;
    instruments_external_estimator_t session_duration_estimator;

    bool use_session_distribution;
    instruments_continuous_distribution_t session_length_distribution;

    // used to do a "double-update" on the first real update,
    //  so that I get an error value in the distribution.
    bool first_update;
    bool was_first_update();

    double last_bw_estimate;
    double last_RTT_estimate;
};

#endif /* _INTNW_INSTRUMENTS_NET_STATS_WRAPPER_H_ */
