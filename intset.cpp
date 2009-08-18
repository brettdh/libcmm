#include "intset.h"
#include <assert.h>

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
void IntSet::insert_vec(std::vector<bool>& vec, long num)
{
    assert(num >= 0);
    if ((size_t)num >= vec.size()) {
        vec.insert(vec.end(), num - vec.size(), false);
        vec.push_back(true);
        assert(vec.size() == ((size_t)num + 1));
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

bool IntSet::contains_vec(const std::vector<bool>& vec, long num) const
{
    assert(num >= 0);
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
