#ifndef _INTNW_INSTRUMENTS_NETWORK_CHOOSER_H_
#define _INTNW_INSTRUMENTS_NETWORK_CHOOSER_H_

#include "intnw_instruments_net_stats_wrapper.h"
#include <instruments.h>

#include <string>

#define NETWORK_CHOICE_WIFI 0
#define NETWORK_CHOICE_CELLULAR 1
#define NETWORK_CHOICE_BOTH 2
#define NUM_STRATEGIES 3

struct strategy_args;
class InstrumentsWrappedNetStats;

class IntNWInstrumentsNetworkChooser {
  public:
    IntNWInstrumentsNetworkChooser(InstrumentsWrappedNetStats *wifi,
                                   InstrumentsWrappedNetStats *cellular);
    ~IntNWInstrumentsNetworkChooser();
    
    int chooseNetwork(int bytelen);

    // for communicating simulated energy/data budgets.
    void setFixedResourceWeights(double energyWeight, double dataWeight);
    void setAdaptiveResourceBudgets(double goalTime, 
                                    int energyBudgetMilliJoules,
                                    int dataBudgetBytes);
    void updateResourceWeights();

    double getEnergyWeight();
    double getDataWeight();
    
    double calculateTransferTime(instruments_context_t ctx,
                                 InstrumentsWrappedNetStats *net_stats,
                                 int bytelen);
    double calculateTransferEnergy(instruments_context_t ctx, 
                                   InstrumentsWrappedNetStats *net_stats,
                                   int bytelen);
    double calculateTransferMobileData(InstrumentsWrappedNetStats *net_stats,
                                       int bytelen);
  private:
    InstrumentsWrappedNetStats *wifi_stats;
    InstrumentsWrappedNetStats *cellular_stats;

    int getStrategyIndex(instruments_strategy_t strategy);
    instruments_strategy_t strategies[NUM_STRATEGIES]; // wifi, cellular, or both
    struct strategy_args *strategy_args[NUM_STRATEGIES - 1];

    instruments_strategy_evaluator_t evaluator;
};

#endif /* _INTNW_INSTRUMENTS_NET_STATS_WRAPPER_H_ */
