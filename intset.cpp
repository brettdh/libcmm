#include "intset.h"

IntSet::IntSet()
    : size_(0)
{
}

void IntSet::insert(long num)
{
    if (!contains(num)) {
        ++size_;
    }
    if (num >= 0) {
        insert_vec(pos_vec, num);
    } else {
        insert_vec(neg_vec, -num);
    }
}
void IntSet::insert_vec(std::vector<bool>& vec, long num)
{
    if (num >= vec.size()) {
        vec.insert(vec.end(), num - vec.size(), false);
        vec.push_back(true);
        assert(vec.size() == (num + 1));
    } else {
        vec[num] = true;
    }
}

bool IntSet::contains(u_long num)
{
    if (num >= 0) {
        return contains_vec(pos_vec, num);
    } else {
        return contains_vec(neg_vec, -num);
    }
}

bool IntSet::contains_vec(const std::vector<bool>& vec, long num)
{
    if (num >= vec.size()) {
        return false;
    } else {
        return vec[num];
    }
}

void
IntSet::print(void)
{
    printf("vec = [");
    for (size_t i = 0; i < vec.size(); i++) {
        printf(vec[i]?"1":"0");
    }
    printf("], size = %lu\n", vec.size());
}

size_t
IntSet::size(void)
{
    return size_;
}
