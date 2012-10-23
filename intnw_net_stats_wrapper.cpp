#include <net_stats.h>
#include "intnw_net_stats_wrapper.h"

static struct timeval mocked_time = {0,0};

static int get_mock_time(struct timeval *tv, void *tzp)
{
    *tv = mocked_time;
    return 0;
}

class static_initer {
  public:
    static_initer() {
        struct timeval tv;
        NetStats::get_time(tv);
        NetStats::set_time_getter(get_mock_time);
    }
};

static static_initer initer;

void set_mock_time(double seconds)
{
    mocked_time.tv_sec = (time_t) seconds;
    mocked_time.tv_usec = (suseconds_t) 
        ((seconds - mocked_time.tv_sec) * 1000000.0);
}

WrappedNetStats::WrappedNetStats(double bw_up, double RTT_seconds)
{
    memset(&local_iface, 0, sizeof(local_iface));
    memset(&remote_iface, 0, sizeof(remote_iface));
    local_iface.bandwidth_up = remote_iface.bandwidth_down = bw_up;
    local_iface.RTT = remote_iface.RTT = (int) (RTT_seconds * 1000.0);

    upstream_stats = new NetStats(local_iface, remote_iface);

    next_event_id = 0;
}

WrappedNetStats::~WrappedNetStats()
{
    delete upstream_stats;
}


void WrappedNetStats::report_upload(int datalen, double start, double duration,
                                    double *bw_out, double *latency_seconds_out)
{
    set_mock_time(start);

    irob_id_t id = next_event_id++;
    upstream_stats->report_send_event(id, datalen);

    set_mock_time(start + duration);

    struct timeval no_delay = {0, 0};
    upstream_stats->report_ack(id, no_delay, no_delay, NULL, bw_out, latency_seconds_out);
}

double WrappedNetStats::get_bandwidth_up()
{
    return get_value(upstream_stats, NET_STATS_BW_UP);
}

double WrappedNetStats::get_latency()
{
    return get_value(upstream_stats, NET_STATS_LATENCY);
}

double WrappedNetStats::get_value(NetStats *stats, unsigned short type)
{
    u_long value = 0;
    if (stats->get_estimate(type, value)) {
        return (double) value;
    } else {
        return -1.0;
    }
}

void WrappedNetStats::update(double bw_up, double RTT_seconds)
{
    local_iface.bandwidth_up = remote_iface.bandwidth_down = bw_up;
    local_iface.RTT = remote_iface.RTT = (int) (RTT_seconds * 1000.0);
    
    upstream_stats->update(local_iface, remote_iface);
}
