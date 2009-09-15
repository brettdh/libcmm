#ifndef intset_h_incl
#define intset_h_incl

#include <boost/dynamic_bitset.hpp>
#include <sys/types.h>



/* (more) space-efficient set of unsigned long integers. */
class IntSet {
  public:
    IntSet();
    void insert(long num);
    bool contains(long num) const;
    void erase(long num);
    //void print(void) const;

    /* returns the number of unique ints insert()ed. */
    size_t size(void) const;
    bool empty(void) const;
    void clear(void);
  private:
    boost::dynamic_bitset<> pos_vec;
    boost::dynamic_bitset<> neg_vec;
    size_t size_;

    void insert_vec(boost::dynamic_bitset<>& vec, long num);
    bool contains_vec(const boost::dynamic_bitset<>& vec, long num) const;
};

#endif
