#include "net_interface.h"
#include "libcmm_net_restriction.h"
#include "debug.h"
#include "common.h"
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

bool network_fits_restriction(u_long labels, 
                             struct net_interface local_iface,
                             struct net_interface remote_iface)
{
    if (!has_network_restriction(labels)) {
        return true;
    }

    StringifyIP local_ip(&local_iface.ip_addr);
    StringifyIP remote_ip(&remote_iface.ip_addr);
    dbgprintf("Checking whether iface pair (%s -> %s) (%s (%d) -> %s (%d)) fits restrictions: %s\n",
              local_ip.c_str(), remote_ip.c_str(), 
              net_type_name(local_iface.type), local_iface.type,
              net_type_name(remote_iface.type), remote_iface.type,
              describe_network_restrictions(labels).c_str());
    
    // At least one of the network restriction labels has been set,
    //  but not all.
    // Go through the list of them and return true as soon as
    //  this network satisfies one of them.
    for (int type = NET_TYPE_WIFI; type < NET_TYPE_WIFI + NUM_NET_TYPES; ++type) {
        if (!network_fits_restriction(type, labels)) {
            dbgprintf("Network type %s doesn't fit label restrictions\n",
                      net_type_name(type));
            continue;
        }
        if (!matches_type(type, local_iface, remote_iface)) {
            dbgprintf("iface pair (%s -> %s) (%s (%d) -> %s (%d)) doesn't fit type %s\n",
                      local_ip.c_str(), remote_ip.c_str(), 
                      net_type_name(local_iface.type), local_iface.type,
                      net_type_name(remote_iface.type), remote_iface.type,
                      net_type_name(type));

            continue;
        }
        dbgprintf("Iface pair fits restrictions.\n");
        return true;
    }

    // At this point, we know this network satisfies none of the
    //  network restrictions, so return false.
    dbgprintf("Iface pair doesn't fit restrictions.\n");
    return false;
}
