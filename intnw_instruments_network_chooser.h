#ifndef _INTNW_INSTRUMENTS_NETWORK_CHOOSER_H_
#define _INTNW_INSTRUMENTS_NETWORK_CHOOSER_H_

#include "network_chooser_impl.h"
#include "redundancy_strategy.h"
#include "intnw_instruments_net_stats_wrapper.h"
#include <instruments.h>

#include <libpowertutor.h>

#include <string>

#define NETWORK_CHOICE_WIFI 0
#define NETWORK_CHOICE_CELLULAR 1
#define NETWORK_CHOICE_BOTH 2
#define NUM_STRATEGIES 3

struct strategy_args;
struct labeled_data;
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
    virtual void scheduleReevaluation(CSockMapping *mapping, 
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

    virtual instruments_strategy_t getChosenStrategy(u_long net_restriction_labels);

    virtual double getEstimatedTransferTime(instruments_context_t context, 
                                            instruments_strategy_t strategy,
                                            u_long send_label,
                                            size_t bytes);
    virtual double getEstimatedTransferEnergy(instruments_context_t context, 
                                              instruments_strategy_t strategy,
                                              u_long send_label,
                                              size_t bytes);
    virtual double getEstimatedTransferData(instruments_context_t context, 
                                            instruments_strategy_t strategy,
                                            u_long send_label,
                                            size_t bytes);

    virtual instruments_estimator_t get_rtt_estimator(u_long net_restriction_labels);
    
  protected:
    virtual void setRedundancyStrategy();
    
  private:
    bool wifi_present;
    bool cellular_present;
    struct timeval wifi_begin;
    
    bool needs_reevaluation;
    int chosen_strategy_type;
    int chosen_singular_strategy_type;
    struct net_interface wifi_local, wifi_remote;
    struct net_interface cellular_local, cellular_remote;

    LabelMatcher label_matcher;

    int chooseNetwork(u_long send_label, int bytelen);

    InstrumentsWrappedNetStats *wifi_stats;
    InstrumentsWrappedNetStats *cellular_stats;

    EnergyComputer cellular_energy_calculator;
    EnergyComputer wifi_energy_calculator;
    void refreshEnergyCalculators();

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
                                 double transfer_time,
                                 double penalty);

    double getCurrentWifiDuration();
    double averageWifiFailoverPenalty();
    
    std::function<void(instruments_strategy_t)> *
        getRedundancyDecisionCallback(CSockMapping *mapping, 
                                      IROBSchedulingData data,
                                      int singular_type);
    instruments_strategy_t getSingularStrategyNotChosen();
    double getReevaluationDelay(PendingSenderIROB *psirob);
    
    
    double calculateTransferTime(instruments_context_t ctx,
                                 InstrumentsWrappedNetStats *net_stats,
                                 struct labeled_data *data);
    double calculateTransferEnergy(instruments_context_t ctx, 
                                   InstrumentsWrappedNetStats *net_stats,
                                   struct labeled_data *data);
    double calculateTransferMobileData(instruments_context_t ctx, 
                                       InstrumentsWrappedNetStats *net_stats,
                                       struct labeled_data *data);

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

bool
is_redundant(int chosen_singular_strategy_type,
             int chosen_strategy_type);

#endif /* _INTNW_INSTRUMENTS_NET_STATS_WRAPPER_H_ */
