#include "intset.h"
#include "debug.h"

IntSet::IntSet()
    : size_(0)
{
}

void IntSet::insert(long num)
{
    if (!contains(num)) {
        ++size_;
        if (num >= 0) {
            insert_vec(pos_vec, num);
        } else {
            insert_vec(neg_vec, -num);
        }
    }
}
void IntSet::insert_vec(boost::dynamic_bitset<>& vec, long num)
{
    ASSERT(num >= 0);
    if ((size_t)num >= vec.size()) {
        vec.resize(num);
        vec.push_back(true);
        ASSERT(vec.size() == ((size_t)num + 1));
    } else {
        vec[num] = true;
    }
}

bool IntSet::contains(long num) const
{
    if (num >= 0) {
        return contains_vec(pos_vec, num);
    } else {
        return contains_vec(neg_vec, -num);
    }
}

bool IntSet::contains_vec(const boost::dynamic_bitset<>& vec, long num) const
{
    ASSERT(num >= 0);
    if ((size_t)num >= vec.size()) {
        return false;
    } else {
        return vec[num];
    }
}

#if 0
void
IntSet::print(void) const
{
    printf("vec = [");
    for (size_t i = 0; i < vec.size(); i++) {
        printf(vec[i]?"1":"0");
    }
    printf("], size = %lu\n", vec.size());
}
#endif

void
IntSet::erase(long num)
{
    if (contains(num)) {
        if (num >= 0) {
            pos_vec[num] = false;
        } else { 
            neg_vec[-num] = false;
        }
        --size_;
    }
}

size_t
IntSet::size(void) const
{
    return size_;
}

bool
IntSet::empty(void) const
{
    return (size() == 0);
}

void
IntSet::clear(void)
{
    pos_vec.clear();
    neg_vec.clear();
    size_ = 0;
}
