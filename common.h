#ifndef common_h
#define common_h

#include <vector>
#include <set>
#include <map>
#include <netinet/in.h>
#include "libcmm.h"
#include <pthread.h>

template <typename IntegerType>
struct IntegerHashCompare {
    size_t hash(IntegerType i) const { return (size_t)i; }
    bool equal(IntegerType i1, IntegerType i2) const { return i1==i2; }
};

struct net_interface {
    struct in_addr ip_addr; /* contents in network byte order as usual */
    u_long labels; /* host byte order */

    bool operator<(const struct net_interface& other) const {
        return ip_addr.s_addr < other.ip_addr.s_addr;
    }
};

class PthreadScopedLock {
  public:
    explicit PthreadScopedLock(pthread_mutex_t *mutex_) : mutex(mutex_) {
        pthread_mutex_lock(mutex);
    }
    ~PthreadScopedLock() {
        pthread_mutex_unlock(mutex);
    }
  private:
    pthread_mutex_t *mutex;
};

typedef std::set<struct net_interface> NetInterfaceSet;

typedef std::vector<std::pair<mc_socket_t, int> > mcSocketOsfdPairList;

#include <stdexcept>

class CMMException : public std::exception { };

template <template <typename, typename> class MapType>
void multimap_erase(MapType& the_map, const MapType::value_type& value)
{
    std::pair<MapType::iterator, MapType::iterator> range = 
        the_map.equal_range(value.first);

    MapType::iterator it = range.first;
    while (it != range.second) {
        if (it->second == value.second) {
            the_map.erase(it++);
        } else {
            ++it;
        }
    }
}

#endif
