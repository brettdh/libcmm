#ifndef common_h
#define common_h

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

#endif
