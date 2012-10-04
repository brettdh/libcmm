#ifndef _REDUNDANCY_STRATEGY_INSTRUMENTS_H_INCLUDED_SHGV42F9Q0VH
#define _REDUNDANCY_STRATEGY_INSTRUMENTS_H_INCLUDED_SHGV42F9Q0VH

#include "redundancy_strategy.h"
#include <sys/types.h>

class RedundancyStrategyInstruments : public RedundancyStrategy {
  public:
    virtual void reportNetStats(int type, 
                                u_long new_observation,
                                u_long current_estimate);
    virtual bool shouldTransmitRedundantly(PendingSenderIROB *psirob);
};

#endif /* _REDUNDANCY_STRATEGY_INSTRUMENTS_H_INCLUDED_SHGV42F9Q0VH */
