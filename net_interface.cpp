#include "net_interface.h"
#include "libcmm_net_preference.h"
#include <functional>
using std::min;

u_long iface_bandwidth(const struct net_interface& local_iface,
                       const struct net_interface& remote_iface)
{
    // from the local sender's perspective.
    u_long bw = min(local_iface.bandwidth_up, remote_iface.bandwidth_down);
    return bw;
}

u_long iface_RTT(const struct net_interface& local_iface,
                 const struct net_interface& remote_iface)
{
    double rtt = 2*((local_iface.RTT / 2.0) + (remote_iface.RTT / 2.0));
    return (u_long)rtt;
}

bool matches_type(int type, 
                  struct net_interface local_iface,
                  struct net_interface remote_iface)
{
    return ((local_iface.type == type && remote_iface.type == type) ||
            (local_iface.type == type && remote_iface.type == 0) ||
            (local_iface.type == 0 && remote_iface.type == type) ||
            (local_iface.type == 0 && remote_iface.type == 0));
}

bool network_fits_preference(int labels, 
                             struct net_interface local_iface,
                             struct net_interface remote_iface)
{
    if (!has_network_preference(labels)) {
        return true;
    }
    
    // At least one of the network preference labels has been set,
    //  but not all.
    // Go through the list of them and return true as soon as
    //  this network satisfies one of them.
    for (int type = NET_TYPE_WIFI; type < NET_TYPE_WIFI + NUM_NET_TYPES; ++type) {
        if (network_fits_preference(type, labels) &&
            matches_type(type, local_iface, remote_iface)) {
            return true;
        }
    }

    // At this point, we know this network satisfies none of the
    //  network preferences, so return false.
    return false;
}
