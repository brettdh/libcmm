#ifndef common_h
#define common_h

#include <vector>
#include <set>
#include <map>
#include "libcmm.h"
#include <signal.h>
#include <cassert>
#include <functional>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

#ifdef ANDROID
typedef short in_port_t;
#endif

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

#define set_nonblocking(fd_arg)                                 \
    do {                                                        \
        int fd = fd_arg;                                        \
        int flags = fcntl(fd, F_GETFL);                         \
        int rc = fcntl(fd, F_SETFL, flags | O_NONBLOCK);        \
        assert(rc == 0);                                        \
    } while (0)

#ifndef handle_error
#define handle_error(cond, str) do { if (cond) { perror(str); exit(-1); } } while (0)
#endif

inline void set_signal(int signo, void (*handler)(int))
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));

    sa.sa_handler = (void (*)(int))handler;
#ifdef SA_INTERRUPT
    sa.sa_flags = SA_INTERRUPT;
#endif
    sigaction(signo, &sa, NULL);
}

int get_unsent_bytes(int sock);

// ip_string must point to a buffer of at least 16 chars
//   (enough to hold an IPv4 a.b.c.d addr, plus NUL)
void get_ip_string(struct in_addr ip_addr, char *ip_string);

#endif
