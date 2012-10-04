#ifndef _REDUNDANCY_STRATEGY_TRIVIAL_H_INCLUDED_219G8FVOBSU9G4
#define _REDUNDANCY_STRATEGY_TRIVIAL_H_INCLUDED_219G8FVOBSU9G4

#include "redundancy_strategy.h"

class AlwaysRedundant : public RedundancyStrategy {
  public:
    virtual bool shouldTransmitRedundantly(PendingSenderIROB *psirob) {
        return true;
    }
};

class NeverRedundant : public RedundancyStrategy {
  public:
    virtual bool shouldTransmitRedundantly(PendingSenderIROB *psirob) {
        return false;
    }
};

#endif /* _REDUNDANCY_STRATEGY_TRIVIAL_H_INCLUDED_219G8FVOBSU9G4 */
