#ifndef _REDUNDANCY_STRATEGY_TRIVIAL_H_INCLUDED_219G8FVOBSU9G4
#define _REDUNDANCY_STRATEGY_TRIVIAL_H_INCLUDED_219G8FVOBSU9G4

#include "redundancy_strategy.h"

class AlwaysRedundant : public RedundancyStrategy {
  public:
    virtual bool shouldTransmitRedundantly(PendingSenderIROB *psirob) {
        return true;
    }
    virtual int getType() { return ALWAYS_REDUNDANT; }
};

class NeverRedundant : public RedundancyStrategy {
  public:
    virtual bool shouldTransmitRedundantly(PendingSenderIROB *psirob) {
        return false;
    }
    virtual int getType() { return INTNW_NEVER_REDUNDANT; }
};

#endif /* _REDUNDANCY_STRATEGY_TRIVIAL_H_INCLUDED_219G8FVOBSU9G4 */
