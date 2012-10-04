#include "redundancy_strategy.h"
#include "redundancy_strategy_trivial.h"
#include "redundancy_strategy_instruments.h"
#include "libcmm.h"

#include <assert.h>

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
