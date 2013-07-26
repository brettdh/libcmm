#ifndef _INTNW_INSTRUMENTS_NETWORK_CHOOSER_H_
#define _INTNW_INSTRUMENTS_NETWORK_CHOOSER_H_

#include "network_chooser_impl.h"
#include "redundancy_strategy.h"
#include "intnw_instruments_net_stats_wrapper.h"
#include <instruments.h>

#include <string>

#define NETWORK_CHOICE_WIFI 0
#define NETWORK_CHOICE_CELLULAR 1
#define NETWORK_CHOICE_BOTH 2
#define NUM_STRATEGIES 3

struct strategy_args;
class InstrumentsWrappedNetStats;
class CSockMapping;

class IntNWInstrumentsNetworkChooser : public NetworkChooserImpl {
  public:
    IntNWInstrumentsNetworkChooser();
    ~IntNWInstrumentsNetworkChooser();
    
    virtual void consider(struct net_interface local_iface, 
                          struct net_interface remote_iface);
    
    virtual bool choose_networks(u_long send_label, size_t num_bytes,
                                 struct net_interface& local_iface,
                                 struct net_interface& remote_iface);

    virtual void reset();

    virtual void checkRedundancyAsync(CSockMapping *mapping,
                                      PendingSenderIROB *psirob, 
                                      const IROBSchedulingData& data);


    // for communicating simulated energy/data budgets.
    void setFixedResourceWeights(double energyWeight, double dataWeight);
    void setAdaptiveResourceBudgets(double goalTime, 
                                    int energyBudgetMilliJoules,
                                    int dataBudgetBytes);

    virtual void reportNetStats(int network_type, 
                                double new_bw,
                                double new_bw_estimate,
                                double new_latency,
                                double new_latency_estimate);

    virtual void reportNetworkSetup(int network_type);
    virtual void reportNetworkTeardown(int network_type);
    
    class RedundancyStrategy : public ::RedundancyStrategy {
      public:
        RedundancyStrategy(IntNWInstrumentsNetworkChooser *chooser_);
        virtual bool shouldTransmitRedundantly(PendingSenderIROB *psirob);
        virtual int getType() { return INTNW_REDUNDANT; }
      private:
        IntNWInstrumentsNetworkChooser *chooser;
    };

    // saves this chooser's distributions to the file
    //  specified by /etc/cmm_config (if specified)
    void saveToFile();

  protected:
    virtual void setRedundancyStrategy();
    
  private:
    bool wifi_present;
    struct timeval wifi_begin;
    
    bool needs_reevaluation;
    int chosen_strategy_type;
    struct net_interface wifi_local, wifi_remote;
    struct net_interface cellular_local, cellular_remote;

    LabelMatcher label_matcher;

    int chooseNetwork(int bytelen);

    InstrumentsWrappedNetStats *wifi_stats;
    InstrumentsWrappedNetStats *cellular_stats;

    int getStrategyIndex(instruments_strategy_t strategy);
    instruments_strategy_t strategies[NUM_STRATEGIES]; // wifi, cellular, or both
    struct strategy_args *strategy_args[NUM_STRATEGIES - 1];

    instruments_strategy_evaluator_t evaluator;

    static std::string getLoadErrorsFilename();
    static std::string getSaveErrorsFilename();
    static bool shouldLoadErrors();
    static bool shouldSaveErrors();

    void loadStats(const std::string& filename);
    void saveStats(const std::string& filename);

    static std::string getLoadStatsFilename();
    static std::string getSaveStatsFilename();
    static bool shouldLoadStats();
    static bool shouldSaveStats();

    void updateResourceWeights();

    double getEnergyWeight();
    double getDataWeight();

    // wrappers to avoid bogus calculations.
    double getBandwidthUp(instruments_context_t ctx,
                          InstrumentsWrappedNetStats *net_stats);
    double getRttSeconds(instruments_context_t ctx,
                         InstrumentsWrappedNetStats *net_stats);
    double getWifiFailurePenalty(instruments_context_t ctx,
                                 InstrumentsWrappedNetStats *net_stats,
                                 double transfer_time);

    double getCurrentWifiDuration();
    
    double calculateTransferTime(instruments_context_t ctx,
                                 InstrumentsWrappedNetStats *net_stats,
                                 int bytelen);
    double calculateTransferEnergy(instruments_context_t ctx, 
                                   InstrumentsWrappedNetStats *net_stats,
                                   int bytelen);
    double calculateTransferMobileData(InstrumentsWrappedNetStats *net_stats,
                                       int bytelen);

    friend double network_transfer_time(instruments_context_t ctx,
                                        void *strategy_arg, 
                                        void *chooser_arg);
    friend double network_transfer_energy_cost(instruments_context_t ctx,
                                               void *strategy_arg, 
                                               void *chooser_arg);
    friend double network_transfer_data_cost(instruments_context_t ctx,
                                             void *strategy_arg, 
                                             void *chooser_arg);
};

#endif /* _INTNW_INSTRUMENTS_NET_STATS_WRAPPER_H_ */
