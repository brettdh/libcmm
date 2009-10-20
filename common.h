#ifndef common_h
#define common_h

#include <vector>
#include <set>
#include <map>
#include "libcmm.h"
#include <cassert>
#include <functional>

template <typename IntegerType>
struct IntegerHashCompare {
    size_t hash(IntegerType i) const { return (size_t)i; }
    bool equal(IntegerType i1, IntegerType i2) const { return i1==i2; }
};

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
