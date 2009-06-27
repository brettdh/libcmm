#include "intset.h"

void IntSet::insert(u_long num)
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
