#ifndef net_interface_h_incl
#define net_interface_h_incl

#include <netinet/in.h>
#include <sys/types.h>
#include <set>

struct net_interface {
    struct in_addr ip_addr; /* contents in network byte order as usual */
    //struct in_addr external_ip_addr; /* not really used yet; for NAT coping */

    // wizard-of-oz network stats.  in reality these depend on both
    //  endpoints and will probably be measured per-CSocket
    //  inside the library.  These might be useful as hints, though,
    //  as supplied by the scout.  If the endpoints exchange
    //  this information as well, each side can compute
    //  a crude estimate of any connection's properties.
    // both of these figures represent measurements conducted
    // on a connection between this endpoint and a remote reference 
    // server.
    //u_long bandwidth; // bytes/sec
    u_long bandwidth_down; // bytes/sec
    u_long bandwidth_up; // bytes/sec
    u_long RTT; // milliseconds
    int type; // NET_TYPE_WIFI or NET_TYPE_THREEG, currently.

    bool operator<(const struct net_interface& other) const {
        return ip_addr.s_addr < other.ip_addr.s_addr;
    }
    bool operator==(const struct net_interface& other) const {
        return ip_addr.s_addr == other.ip_addr.s_addr;
    }
};

u_long iface_bandwidth(const struct net_interface& local_iface,
                       const struct net_interface& remote_iface);
u_long iface_RTT(const struct net_interface& local_iface,
                 const struct net_interface& remote_iface);

bool matches_type(int type, 
                  struct net_interface local_iface,
                  struct net_interface remote_iface);
int get_network_type(struct net_interface local_iface,
                     struct net_interface remote_iface);
bool network_fits_restriction(u_long labels, 
                              struct net_interface local_iface,
                              struct net_interface remote_iface);

typedef std::set<struct net_interface> NetInterfaceSet;

#endif
