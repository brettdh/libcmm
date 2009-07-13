#ifndef intset_h_incl
#define intset_h_incl

#include <vector>
#include <sys/types.h>

/* (more) space-efficient set of unsigned long integers. */
class IntSet {
  public:
    IntSet();
    void insert(long num);
    bool contains(long num) const;
    //void print(void) const;

    /* returns the number of unique ints insert()ed. */
    size_t size(void) const;
  private:
    std::vector<bool> pos_vec;
    std::vector<bool> neg_vec;
    size_t size_;

    void insert_vec(std::vector<bool>& vec, long num);
    bool contains_vec(const std::vector<bool>& vec, long num) const;
};

#endif
