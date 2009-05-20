#ifndef common_h
#define common_h

template <typename T>
struct MyHashCompare {
    size_t hash(T sock) const { return (size_t)sock; }
    bool equal(T s1, T s2) const { return s1==s2; }
};

#endif
