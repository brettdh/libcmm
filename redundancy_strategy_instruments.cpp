#include "redundancy_strategy_instruments.h"
#include <sys/types.h>

void 
RedundancyStrategyInstruments::reportNetStats(int type, 
                                              u_long new_observation,
                                              u_long current_estimate)
{
    // TODO-REDUNDANCY: implement
}

bool 
RedundancyStrategyInstruments::
shouldTransmitRedundantly(PendingSenderIROB *psirob)
{
    // TODO-REDUNDANCY: implement
    return false;
}
