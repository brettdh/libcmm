#include "redundancy_strategy.h"
#include "redundancy_strategy_trivial.h"
#include "intnw_instruments_network_chooser.h"
#include "libcmm.h"

#include <assert.h>
#include <string.h>

const char *
RedundancyStrategy::strategy_types[NUM_REDUNDANCY_STRATEGY_TYPES] = {
    "intnw_never_redundant", "always_redundant", "intnw_redundant",
    "cellular_only", "wifi_preferred"
};

std::string
RedundancyStrategy::describe_type(int type)
{
    assert(type >= INTNW_NEVER_REDUNDANT && type < NUM_REDUNDANCY_STRATEGY_TYPES);
    return strategy_types[type];
}

int
RedundancyStrategy::get_type(const std::string& name)
{
    for (int i = INTNW_NEVER_REDUNDANT; i < NUM_REDUNDANCY_STRATEGY_TYPES; ++i) {
        if (strcasecmp(name.c_str(), strategy_types[i]) == 0) {
            return i;
        }
    }
    return -1;
}

RedundancyStrategy *
RedundancyStrategy::create(int type)
{
    switch (type) {
    case INTNW_NEVER_REDUNDANT:
    case CELLULAR_ONLY:
    case WIFI_PREFERRED:
        return new NeverRedundant;
    case ALWAYS_REDUNDANT:
        return new AlwaysRedundant;
    case INTNW_REDUNDANT:
        // shouldn't be here.  the Chooser calls the constructor itself.
    default:
        assert(0);
    }
}

RedundancyStrategy::RedundancyStrategy()
{
}
