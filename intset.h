#ifndef intset_h_incl
#define intset_h_incl

#include <vector>
#include <sys/types.h>

/* (more) space-efficient set of unsigned long integers. */
class IntSet {
  public:
    void insert(u_long num);
    bool contains(u_long num);
    void print(void);

    /* returns the number of unique ints insert()ed. */
    size_t size(void);
  private:
    std::vector<bool> vec;
    size_t size_;
};
