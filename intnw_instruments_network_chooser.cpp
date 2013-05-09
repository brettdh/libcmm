#include "intnw_instruments_network_chooser.h"
#include "intnw_instruments_net_stats_wrapper.h"
#include "libcmm_net_restriction.h"
#include "debug.h"
#include "config.h"
#include <assert.h>
#include "libpowertutor.h"

#include <functional>
#include <sstream>
#include <string>
using std::max; using std::ostringstream;
using std::string;

#include <instruments_private.h>
#include <resource_weights.h>

static const char *strategy_names[NUM_STRATEGIES] = {
    "wifi", "3G", "redundant"
};

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
IntNWInstrumentsNetworkChooser::getBandwidthUp(instruments_context_t ctx,
                                               InstrumentsWrappedNetStats *net_stats)
{
    double bw = net_stats->get_bandwidth_up(ctx);
    return max(1.0, bw); // TODO: revisit.  This is a hack to avoid bogus calculations.
}

double
IntNWInstrumentsNetworkChooser::getRttSeconds(instruments_context_t ctx,
                                              InstrumentsWrappedNetStats *net_stats)
{
    double rtt_seconds = net_stats->get_rtt(ctx);
    return max(0.0, rtt_seconds); // TODO: revisit.  This is a hack to avoid bogus calculations.
}

double
IntNWInstrumentsNetworkChooser::calculateTransferTime(instruments_context_t ctx,
                                                      InstrumentsWrappedNetStats *net_stats,
                                                      int bytelen)
{
    assert(net_stats);
    double bw = getBandwidthUp(ctx, net_stats);
    double rtt_seconds = getRttSeconds(ctx, net_stats);

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
    
    double bw = getBandwidthUp(ctx, net_stats);
    double rtt_seconds = getRttSeconds(ctx, net_stats);
    
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

    wifi_stats = new InstrumentsWrappedNetStats("wifi");
    cellular_stats = new InstrumentsWrappedNetStats("cellular");

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

    EvalMethod method = Config::getInstance()->getEstimatorErrorEvalMethod();
    evaluator = register_strategy_set_with_method(strategies, NUM_STRATEGIES, method);

    if (shouldLoadErrors()) {
        restore_evaluator(evaluator, getLoadErrorsFilename().c_str());
    }
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

    dbgprintf("chooseNetwork: %s\n", strategy_names[chosen_strategy_type]);

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

    label_matcher.reset();
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

    ostringstream oss;
    oss << "Adding new stats to " << net_type_name(network_type) 
        << " network estimator:   ";
    if (new_bw > 0.0) {
        oss << "bandwidth: obs " << new_bw << " est " << new_bw_estimate;
    }
    if (new_latency_seconds > 0.0) {
        oss << "  latency: obs " << new_latency_seconds << " est " << new_latency_estimate;
    }
    oss << "\n";
    dbgprintf("%s", oss.str().c_str());
    
    // XXX: hackish.  Should separate bw and RTT updates.
    stats->update(new_bw, new_bw_estimate, 
                  new_latency_seconds, new_latency_estimate);
    needs_reevaluation = true;
}

void
IntNWInstrumentsNetworkChooser::setRedundancyStrategy()
{
    dbgprintf("setting IntNWInstrumentsNetworkChooser::RedundancyStrategy\n");
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
    dbgprintf("shouldTransmitRedundantly: Chosen network strategy: %s\n",
              strategy_names[chooser->chosen_strategy_type]);
    return (chooser->chosen_strategy_type == NETWORK_CHOICE_BOTH);
}


bool
IntNWInstrumentsNetworkChooser::shouldLoadErrors()
{
    return !getLoadErrorsFilename().empty();
}
bool
IntNWInstrumentsNetworkChooser::shouldSaveErrors()
{
    return !getSaveErrorsFilename().empty();
}

std::string
IntNWInstrumentsNetworkChooser::getLoadErrorsFilename()
{
    return Config::getInstance()->getEstimatorErrorLoadFilename();
}

std::string
IntNWInstrumentsNetworkChooser::getSaveErrorsFilename()
{
    return Config::getInstance()->getEstimatorErrorSaveFilename();
}

void
IntNWInstrumentsNetworkChooser::saveToFile()
{
    if (shouldSaveErrors()) {
        string filename = getSaveErrorsFilename();
        dbgprintf("Saving IntNWInstrumentsNetworkChooser %p to %s\n",
                  this, filename.c_str());
        save_evaluator(evaluator, filename.c_str());
    }
}
