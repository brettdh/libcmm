#include "net_interface.h"
#include <functional>
using std::min;

u_long iface_bandwidth(const struct net_interface& local_iface,
                       const struct net_interface& remote_iface)
{
    u_long bw = min(local_iface.bandwidth, remote_iface.bandwidth);
    return bw;
}

u_long iface_RTT(const struct net_interface& local_iface,
                 const struct net_interface& remote_iface)
{
    double rtt = 2*((local_iface.RTT / 2.0) + (remote_iface.RTT / 2.0));
    return rtt;
}
