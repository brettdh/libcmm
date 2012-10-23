#ifndef _INTNW_NET_STATS_WRAPPER_H_
#define _INTNW_NET_STATS_WRAPPER_H_

#include <libcmm_irob.h>
#include <net_interface.h>
#include <stdlib.h>

class NetStats;

class WrappedNetStats {
  public:
    WrappedNetStats(double bw_up, double RTT_seconds);
    ~WrappedNetStats();
    void report_upload(int datalen, double start, double duration,
                       double *bw_out=NULL, double *latency_seconds_out=NULL);

    // add active measurement result to stats.
    void update(double bw_up, double RTT_seconds);

    double get_bandwidth_up();
    double get_latency(); // return the one-way latency.
  private:
    double get_value(NetStats *stats, unsigned short type);

    NetStats *upstream_stats;

    struct net_interface local_iface;
    struct net_interface remote_iface;

    irob_id_t next_event_id;
};

void set_mock_time(double seconds);

#endif /* _INTNW_NET_STATS_WRAPPER_H_ */
