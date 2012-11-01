#include "intnw_instruments_network_chooser.h"
#include "intnw_instruments_net_stats_wrapper.h"
#include "libcmm_net_restriction.h"
#include "debug.h"
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

double
network_transfer_time(instruments_context_t ctx, void *strategy_arg, void *chooser_arg)
{
    struct strategy_args *args = (struct strategy_args *)strategy_arg;
    int bytelen = (int) chooser_arg;
    assert(args->net_stats);
    return args->chooser->calculateTransferTime(ctx, *args->net_stats, 
                                                bytelen);
}

double
network_transfer_energy_cost(instruments_context_t ctx, void *strategy_arg, void *chooser_arg)
{
    struct strategy_args *args = (struct strategy_args *)strategy_arg;
    int bytelen = (int) chooser_arg;
    assert(args->net_stats);

    return args->chooser->calculateTransferEnergy(ctx, *args->net_stats,
                                                  bytelen);
}

double
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


IntNWInstrumentsNetworkChooser::IntNWInstrumentsNetworkChooser()
    : wifi_present(false), needs_reevaluation(true), chosen_strategy_type(-1)
{
    dbgprintf("creating InstrumentsNetworkChooser %p\n", this);

    wifi_stats = new InstrumentsWrappedNetStats;
    cellular_stats = new InstrumentsWrappedNetStats;

    for (int i = 0; i < NUM_STRATEGIES - 1; ++i) {
        strategy_args[i] = new struct strategy_args;
        strategy_args[i]->chooser = this;
    }
    strategy_args[NETWORK_CHOICE_WIFI]->net_stats = &wifi_stats;
    strategy_args[NETWORK_CHOICE_CELLULAR]->net_stats = &cellular_stats;

    // BUG: for some reason, the net_stats pointer-pointer that gets set here
    // BUG:  is getting clobbered at some point.  wifi_stats and cellular_stats
    // BUG:  themselves are fine; it's the strategy_args content that gets clobbered.
    
    for (int i = NETWORK_CHOICE_WIFI; i <= NETWORK_CHOICE_CELLULAR; ++i) {
        strategies[i] = 
            make_strategy(network_transfer_time, 
                          network_transfer_energy_cost,
                          network_transfer_data_cost, 
                          (void *)strategy_args[i], (void *) 0);
    }

    strategies[NETWORK_CHOICE_BOTH] = make_redundant_strategy(strategies, 2);

    EvalMethod method = EMPIRICAL_ERROR_ALL_SAMPLES_INTNW;
    //EvalMethod method = EMPIRICAL_ERROR_BINNED_INTNW;
    //EvalMethod method = EMPIRICAL_ERROR_BINNED;

    evaluator = register_strategy_set_with_method(strategies, NUM_STRATEGIES, method);
}

IntNWInstrumentsNetworkChooser::~IntNWInstrumentsNetworkChooser()
{
    dbgprintf("destroying InstrumentsNetworkChooser %p\n", this);
    
    delete wifi_stats;
    delete cellular_stats;

    free_strategy_evaluator(evaluator);

    for (int i = 0; i < NUM_STRATEGIES; ++i) {
        free_strategy(strategies[i]);
    }
    for (int i = NETWORK_CHOICE_WIFI; i <= NETWORK_CHOICE_CELLULAR; ++i) {
        delete strategy_args[i];
    }
}

bool 
IntNWInstrumentsNetworkChooser::
choose_networks(u_long send_label, size_t num_bytes,
                struct net_interface& local_iface,
                struct net_interface& remote_iface)
{
    // XXX:  don't really want to require this, but it's 
    // XXX:   true for the redundancy experiments.
    // TODO: make sure that CSockMapping calls the 
    // TODO:  right version of choose_networks.
    if (num_bytes == 0) {
        // TODO: can we apply the brute force method
        // TODO:  without knowing the size of the data?
        return label_matcher.choose_networks(send_label, num_bytes,
                                             local_iface, remote_iface);
    }

    assert(has_match);
    
    if (!wifi_present) {
        local_iface = cellular_local;
        remote_iface = cellular_remote;
        return true;
    }

    if (needs_reevaluation) {
        chosen_strategy_type = chooseNetwork(num_bytes);
        needs_reevaluation = false;
    }

    if (chosen_strategy_type == NETWORK_CHOICE_WIFI ||
        chosen_strategy_type == NETWORK_CHOICE_BOTH) {
        local_iface = wifi_local;
        remote_iface = wifi_remote;
    } else {
        assert(chosen_strategy_type == NETWORK_CHOICE_CELLULAR);
        local_iface = cellular_local;
        remote_iface = cellular_remote;
    }
    return true;
}

void 
IntNWInstrumentsNetworkChooser::consider(struct net_interface local_iface, 
                                         struct net_interface remote_iface)
{
    // pass it to a delegate matcher in case choose_networks
    //  gets called without knowing how many bytes will be sent
    label_matcher.consider(local_iface, remote_iface);
    
    has_match = true;
    if (matches_type(NET_TYPE_WIFI, local_iface, remote_iface)) {
        wifi_present = true;
        wifi_local = local_iface;
        wifi_remote = remote_iface;
    } else if (matches_type(NET_TYPE_THREEG, local_iface, remote_iface)) {
        cellular_local = local_iface;
        cellular_remote = remote_iface;
    } else assert(false);
}

void
IntNWInstrumentsNetworkChooser::reset()
{
    NetworkChooserImpl::reset();
    wifi_present = false;
    needs_reevaluation = true;
    chosen_strategy_type = -1;
}



int 
IntNWInstrumentsNetworkChooser::chooseNetwork(int bytelen)
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

void
IntNWInstrumentsNetworkChooser::reportNetStats(int network_type, 
                                               double new_bw,
                                               double new_bw_estimate,
                                               double new_latency_seconds,
                                               double new_latency_estimate)
{
    InstrumentsWrappedNetStats *stats = NULL;
    switch (network_type) {
    case NET_TYPE_WIFI:
        stats = wifi_stats;
        break;
    case NET_TYPE_THREEG:
        stats = cellular_stats;
        break;
    default:
        assert(false);
        break;
    }

    dbgprintf("Adding new stats to %s network estimator\n"
              "   bandwidth: obs %f est %f  latency: obs %f est %f\n",
              net_type_name(network_type),
              new_bw, new_bw_estimate, new_latency_seconds, new_latency_estimate);
    stats->update(new_bw, new_bw_estimate, 
                  new_latency_seconds, new_latency_estimate);
    needs_reevaluation = true;
}

void
IntNWInstrumentsNetworkChooser::setRedundancyStrategy()
{
    assert(redundancyStrategy == NULL);
    redundancyStrategy = 
        new IntNWInstrumentsNetworkChooser::RedundancyStrategy(this);
}

IntNWInstrumentsNetworkChooser::RedundancyStrategy::
RedundancyStrategy(IntNWInstrumentsNetworkChooser *chooser_)
    : chooser(chooser_)
{
}

bool 
IntNWInstrumentsNetworkChooser::RedundancyStrategy::
shouldTransmitRedundantly(PendingSenderIROB *psirob)
{
    assert(chooser->has_match);
    assert(chooser->chosen_strategy_type != -1);
    return (chooser->chosen_strategy_type == NETWORK_CHOICE_BOTH);
}
