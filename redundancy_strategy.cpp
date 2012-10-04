#include "redundancy_strategy.h"
#include "redundancy_strategy_trivial.h"
#include "redundancy_strategy_instruments.h"
#include "libcmm.h"

#include <assert.h>

const char *
RedundancyStrategy::strategy_types[NUM_REDUNDANCY_STRATEGY_TYPES] = {
    "never-redundant", "always-redundant", "evaluate-redundancy"
};

std::string
RedundancyStrategy::describe_type(int type)
{
    assert(type >= 0 && type < NUM_REDUNDANCY_STRATEGY_TYPES);
    return strategy_types[type];
}

RedundancyStrategy *
RedundancyStrategy::create(int type)
{
    switch (type) {
    case NEVER_REDUNDANT:
        return new NeverRedundant;
    case ALWAYS_REDUNDANT:
        return new AlwaysRedundant;
    case EVALUATE_REDUNDANCY:
        return new RedundancyStrategyInstruments;
    default:
        assert(0);
    }
}

RedundancyStrategy::RedundancyStrategy()
{
}
