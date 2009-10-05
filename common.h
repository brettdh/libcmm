#ifndef common_h
#define common_h

#include <vector>
#include <set>
#include <map>
#include <netinet/in.h>
#include "libcmm.h"
#include <cassert>

template <typename IntegerType>
struct IntegerHashCompare {
    size_t hash(IntegerType i) const { return (size_t)i; }
    bool equal(IntegerType i1, IntegerType i2) const { return i1==i2; }
};

struct net_interface {
    struct in_addr ip_addr; /* contents in network byte order as usual */
    u_long labels; /* host byte order */

    // wizard-of-oz network stats.  in reality these depend on both
    //  endpoints and will probably be measured per-CSocket
    //  inside the library.  These might be useful as hints, though,
    //  as supplied by the scout.  If the endpoints exchange
    //  this information as well, each side can compute
    //  a crude estimate of any connection's properties.
    // both of these figures represent measurements conducted
    // on a connection between this endpoint and a remote reference 
    // server.
    u_long bandwidth; // bytes/sec
    u_long RTT; // milliseconds

    bool operator<(const struct net_interface& other) const {
        return ip_addr.s_addr < other.ip_addr.s_addr;
    }
};

typedef std::set<struct net_interface> NetInterfaceSet;

typedef std::vector<std::pair<mc_socket_t, int> > mcSocketOsfdPairList;

#include <stdexcept>

class CMMException : public std::exception { };

template <typename MapType>
void multimap_erase(MapType& the_map, const typename MapType::value_type& value)
{
    std::pair<typename MapType::iterator, 
              typename MapType::iterator> range = 
        the_map.equal_range(value.first);
    
    typename MapType::iterator it = range.first;
    while (it != range.second) {
        if (it->second == value.second) {
            the_map.erase(it++);
        } else {
            ++it;
        }
    }
}

template <typename ItemType, typename ContainerType>
bool pop_item(ContainerType& container, ItemType& item)
{
    if (container.empty()) {
        return false;
    }
    
    item = *container.begin();
    container.erase(container.begin());
    return true;
}

#endif
