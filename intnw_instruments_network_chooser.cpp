#include "intnw_instruments_network_chooser.h"
#include <assert.h>
#include "libpowertutor.h"

#include <functional>
using std::max;

#include <instruments_private.h>
#include <resource_weights.h>

struct strategy_args {
    IntNWInstrumentsNetworkChooser *chooser;
    InstrumentsWrappedNetStats **net_stats;
};

static double
network_transfer_time(instruments_context_t ctx, void *strategy_arg, void *chooser_arg)
{
    struct strategy_args *args = (struct strategy_args *)strategy_arg;
    int bytelen = (int) chooser_arg;
    assert(args->net_stats);
    return args->chooser->calculateTransferTime(ctx, *args->net_stats, 
                                                bytelen);
}

static double
network_transfer_energy_cost(instruments_context_t ctx, void *strategy_arg, void *chooser_arg)
{
    struct strategy_args *args = (struct strategy_args *)strategy_arg;
    int bytelen = (int) chooser_arg;
    assert(args->net_stats);

    return args->chooser->calculateTransferEnergy(ctx, *args->net_stats,
                                                  bytelen);
}

static double
network_transfer_data_cost(instruments_context_t ctx, void *strategy_arg, void *chooser_arg)
{
    struct strategy_args *args = (struct strategy_args *)strategy_arg;
    int bytelen = (int) chooser_arg;
    assert(args->net_stats);
    
    return args->chooser->calculateTransferMobileData(*args->net_stats, bytelen);
}

double
IntNWInstrumentsNetworkChooser::calculateTransferTime(instruments_context_t ctx,
                                                      InstrumentsWrappedNetStats *net_stats,
                                                      int bytelen)
{
    assert(net_stats);
    double bw = net_stats->get_bandwidth_up(ctx);
    double rtt_seconds = net_stats->get_rtt(ctx);

    bw = max(1.0, bw); // TODO: revisit.  This is a hack to avoid bogus calculations.
    rtt_seconds = max(0.0, rtt_seconds);
    return (bytelen / bw) + rtt_seconds;
}

double
IntNWInstrumentsNetworkChooser::calculateTransferEnergy(instruments_context_t ctx, 
                                                        InstrumentsWrappedNetStats *net_stats,
                                                        int bytelen)
{
    assert(net_stats);

    NetworkType type;
    if (net_stats == wifi_stats) {
        type = TYPE_WIFI;
    } else if (net_stats == cellular_stats) {
        type = TYPE_MOBILE;
    } else assert(0);
    
    double bw = net_stats->get_bandwidth_up(ctx);
    double rtt_seconds = net_stats->get_rtt(ctx);
    
    return estimate_energy_cost(type, bytelen, bw, rtt_seconds * 1000.0);
}

double
IntNWInstrumentsNetworkChooser::calculateTransferMobileData(InstrumentsWrappedNetStats *net_stats,
                                                            int bytelen)
{
    if (net_stats == wifi_stats) {
        return 0;
    } else if (net_stats == cellular_stats) {
        return bytelen;
    } else assert(0);
}


IntNWInstrumentsNetworkChooser::IntNWInstrumentsNetworkChooser(
    InstrumentsWrappedNetStats *wifi,
    InstrumentsWrappedNetStats *cellular
)
{
    // no ownership transfer here.
    wifi_stats = wifi;
    cellular_stats = cellular;

    for (int i = 0; i < NUM_STRATEGIES - 1; ++i) {
        strategy_args[i] = new struct strategy_args;
        strategy_args[i]->chooser = this;
    }
    strategy_args[NETWORK_CHOICE_WIFI]->net_stats = &wifi_stats;
    strategy_args[NETWORK_CHOICE_CELLULAR]->net_stats = &cellular_stats;
    
    for (int i = NETWORK_CHOICE_WIFI; i <= NETWORK_CHOICE_CELLULAR; ++i) {
        strategies[i] = 
            make_strategy(network_transfer_time, 
                          network_transfer_energy_cost,
                          network_transfer_data_cost, 
                          (void *)strategy_args[i], (void *) 0);
    }

    strategies[NETWORK_CHOICE_BOTH] = make_redundant_strategy(strategies, 2);

    //EvalMethod method = EMPIRICAL_ERROR_ALL_SAMPLES_INTNW;
    EvalMethod method = EMPIRICAL_ERROR_BINNED_INTNW;
    //EvalMethod method = EMPIRICAL_ERROR_BINNED;

    evaluator = register_strategy_set_with_method(strategies, NUM_STRATEGIES, method);

    wifi_stats->update();
    cellular_stats->update();
}

IntNWInstrumentsNetworkChooser::~IntNWInstrumentsNetworkChooser()
{
    free_strategy_evaluator(evaluator);

    for (int i = 0; i < NUM_STRATEGIES; ++i) {
        free_strategy(strategies[i]);
    }
    for (int i = NETWORK_CHOICE_WIFI; i <= NETWORK_CHOICE_CELLULAR; ++i) {
        delete strategy_args[i];
    }
}

int IntNWInstrumentsNetworkChooser::chooseNetwork(int bytelen)
{
    instruments_strategy_t chosen = choose_strategy(evaluator, (void *)bytelen);
    return getStrategyIndex(chosen);
}

int IntNWInstrumentsNetworkChooser::getStrategyIndex(instruments_strategy_t strategy)
{
    for (int i = 0; i < NUM_STRATEGIES; ++i) {
        if (strategy == strategies[i]) {
            return i;
        }
    }
    assert(0);
}

void 
IntNWInstrumentsNetworkChooser::setFixedResourceWeights(double energyWeight, 
                                                        double dataWeight)
{
    set_fixed_resource_weights(energyWeight, dataWeight);
}

static int energy_spent_mJ = 0;
static int data_spent_bytes = 0;

void 
IntNWInstrumentsNetworkChooser::setAdaptiveResourceBudgets(double goalTime, 
                                                           int energyBudgetMilliJoules,
                                                           int dataBudgetBytes)
{
    struct timeval goaltime_tv;
    goaltime_tv.tv_sec = static_cast<time_t>(goalTime);
    goaltime_tv.tv_usec = (goalTime - goaltime_tv.tv_sec) * 1000000;

    energy_spent_mJ = 0;
    data_spent_bytes = 0;

    set_resource_budgets(goaltime_tv, energyBudgetMilliJoules, dataBudgetBytes);
}

void IntNWInstrumentsNetworkChooser::updateResourceWeights()
{
    int new_energy_spent_mJ = energy_consumed_since_reset();
    int new_data_spent_bytes = mobile_bytes_consumed_since_reset();
    report_spent_energy(new_energy_spent_mJ - energy_spent_mJ);
    report_spent_data(new_data_spent_bytes - data_spent_bytes);
    update_weights_now();

    energy_spent_mJ = new_energy_spent_mJ;
    data_spent_bytes = new_data_spent_bytes;
}

double IntNWInstrumentsNetworkChooser::getEnergyWeight()
{
    return get_energy_cost_weight();
}

double IntNWInstrumentsNetworkChooser::getDataWeight()
{
    return get_data_cost_weight();
}
