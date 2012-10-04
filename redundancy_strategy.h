#ifndef _REDUNDANCY_STRATEGY_H_INCLUDED_08Y2FHVUOBQ
#define _REDUNDANCY_STRATEGY_H_INCLUDED_08Y2FHVUOBQ

class PendingSenderIROB;

class RedundancyStrategy {
  public:
    virtual bool shouldTransmitRedundantly(PendingSenderIROB *psirob) = 0;
    static RedundancyStrategy *create(int type);
  protected:
    RedundancyStrategy();
};

#endif /* _REDUNDANCY_STRATEGY_H_INCLUDED_08Y2FHVUOBQ */
