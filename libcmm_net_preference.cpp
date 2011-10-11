#include "libcmm_net_preference.h"

static const char *net_type_strs[NUM_NET_TYPES + 1] = {"wifi", "3G", "unknown"};

const char *net_type_name(int type)
{
    int index = type - 1;
    if (index < 0 || index >= NUM_NET_TYPES) {
        index = NUM_NET_TYPES;
    }
    return net_type_strs[index];
}
