#ifndef _REDUNDANCY_STRATEGY_H_INCLUDED_08Y2FHVUOBQ
#define _REDUNDANCY_STRATEGY_H_INCLUDED_08Y2FHVUOBQ

#include <string>
#include "libcmm.h"

class PendingSenderIROB;

class RedundancyStrategy {
  public:
    virtual bool shouldTransmitRedundantly(PendingSenderIROB *psirob) = 0;

    static RedundancyStrategy *create(int type);
    static std::string describe_type(int type);
    static int get_type(const std::string& name);

    virtual int getType() = 0;
  protected:
    RedundancyStrategy();

  private:
    static const char *strategy_types[NUM_REDUNDANCY_STRATEGY_TYPES];
};

#endif /* _REDUNDANCY_STRATEGY_H_INCLUDED_08Y2FHVUOBQ */
