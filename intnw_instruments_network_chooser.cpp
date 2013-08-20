#include "intnw_instruments_network_chooser.h"
#include "intnw_instruments_net_stats_wrapper.h"
#include "libcmm_net_restriction.h"
#include "debug.h"
#include "config.h"
#include "csocket_mapping.h"
#include "pending_sender_irob.h"
#include "timeops.h"
#include <assert.h>
#include "libpowertutor.h"

#include <functional>
#include <sstream>
#include <string>
#include <fstream>
using std::max; using std::ostringstream;
using std::string; using std::function;
using std::ifstream; using std::ofstream; using std::endl;

using intnw::check;

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

// I've seen variation from 7-14 seconds here, approximately, so I'll just
//  pick something in the middle.
// TODO: measure it, many times, and take an average.
// XXX: this should be an actual average; e.g. half the average of failure measured as 
// XXX:  time from radio-off to interface-down.
// XXX: or, the average delay actually experienced by failed wifi transfers.
//static double WIFI_FAILURE_PENALTY = 10.0;
// DONE: measured the time from radio-off to interface down.
//       It's 10.864 (0.090) for 21 samples.
//       Stored this in the config file
//       and now we divide it by two for the average 
//       failover delay actually experienced by each failed wifi transfer.

double
IntNWInstrumentsNetworkChooser::getWifiFailurePenalty(instruments_context_t ctx,
                                                      InstrumentsWrappedNetStats *net_stats,
                                                      double transfer_time)
{
    double penalty = 0.0;
    if (net_stats == wifi_stats) {
        double current_wifi_duration = getCurrentWifiDuration();
        penalty = wifi_stats->getWifiFailurePenalty(ctx, transfer_time, 
                                                    current_wifi_duration, 
                                                    Config::getInstance()->getWifiFailoverDelay());
    }
    return penalty;
}

double
IntNWInstrumentsNetworkChooser::calculateTransferTime(instruments_context_t ctx,
                                                      InstrumentsWrappedNetStats *net_stats,
                                                      int bytelen)
{
    assert(net_stats);
    double bw = getBandwidthUp(ctx, net_stats);
    double rtt_seconds = getRttSeconds(ctx, net_stats);
    double tx_time = (bytelen / bw) + rtt_seconds;
    double wifi_failure_penalty = getWifiFailurePenalty(ctx, net_stats, tx_time);

    return tx_time + wifi_failure_penalty;
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
    
    // TODO: incorporate wifi failure_penalty somehow?
    // TODO: e.g. energy cost of sending on cellular anyway
    return estimate_energy_cost(type, bytelen, bw, rtt_seconds * 1000.0);
}

double
IntNWInstrumentsNetworkChooser::calculateTransferMobileData(InstrumentsWrappedNetStats *net_stats,
                                                            int bytelen)
{
    if (net_stats == wifi_stats) {
        // TODO: incorporate wifi failure_penalty somehow?
        // TODO: e.g. data cost of sending on cellular anyway
        return 0;
    } else if (net_stats == cellular_stats) {
        return bytelen;
    } else assert(0);
}


IntNWInstrumentsNetworkChooser::IntNWInstrumentsNetworkChooser()
    : wifi_present(false), needs_reevaluation(true), chosen_strategy_type(-1), chosen_singular_strategy_type(-1)
{
    dbgprintf("creating InstrumentsNetworkChooser %p\n", this);

    cellular_stats = new InstrumentsWrappedNetStats("cellular");
    wifi_stats = new InstrumentsWrappedNetStats("wifi");
    // TODO: sensible (or historical) initial value for wifi session length

    wifi_begin.tv_sec = wifi_begin.tv_usec = 0;

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
        set_strategy_name(strategies[i], strategy_names[i]);
    }

    strategies[NETWORK_CHOICE_BOTH] = make_redundant_strategy(strategies, 2);
    set_strategy_name(strategies[NETWORK_CHOICE_BOTH], strategy_names[NETWORK_CHOICE_BOTH]);

    EvalMethod method = Config::getInstance()->getEstimatorErrorEvalMethod();
    evaluator = register_strategy_set_with_method(strategies, NUM_STRATEGIES, method);

    if (shouldLoadErrors()) {
        restore_evaluator(evaluator, getLoadErrorsFilename().c_str());
    }
    
    if (shouldLoadStats()) {
        loadStats(getLoadStatsFilename());
    } else {
        // reasonable non-zero initial estimate, with the same effect as 
        // having a zero inital estimate (since the failover delay
        // is considered part of the session)
        struct timeval init = { (time_t) Config::getInstance()->getWifiFailoverDelay(), 0 };
        wifi_stats->addSessionDuration(init);
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
        dbgprintf("About to choose a network; current wifi session length: %f seconds\n",
                  getCurrentWifiDuration());
        struct timeval begin, end, duration;
        gettimeofday(&begin, NULL);
        chosen_singular_strategy_type = chooseNetwork(num_bytes);
        gettimeofday(&end, NULL);
        timersub(&end, &begin, &duration);
        dbgprintf("chooseNetwork: %s   took %lu.%06lu seconds\n", 
                  strategy_names[chosen_singular_strategy_type],
                  duration.tv_sec, duration.tv_usec);

        needs_reevaluation = false;
    }

    if (chosen_singular_strategy_type == NETWORK_CHOICE_WIFI) {
        local_iface = wifi_local;
        remote_iface = wifi_remote;
    } else {
        assert(chosen_singular_strategy_type == NETWORK_CHOICE_CELLULAR);
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
    chosen_singular_strategy_type = -1;

    label_matcher.reset();
}



int 
IntNWInstrumentsNetworkChooser::chooseNetwork(int bytelen)
{
    wifi_stats->setWifiSessionLengthBound(getCurrentWifiDuration());

    instruments_strategy_t chosen = choose_nonredundant_strategy(evaluator, (void *)bytelen);
    return getStrategyIndex(chosen);
}

static void 
pre_eval_callback_wrapper(void *arg)
{
    auto *fn = static_cast<function<void()> *>(arg);
    (*fn)();
    delete fn;
}

static void 
chosen_strategy_callback_wrapper(instruments_strategy_t strategy, void *arg)
{
    auto *fn = static_cast<function<void(instruments_strategy_t)> *>(arg);
    (*fn)(strategy);
    delete fn;
}

function<void(instruments_strategy_t)> *
IntNWInstrumentsNetworkChooser::getRedundancyDecisionCallback(CSockMapping *mapping, 
                                                              const IROBSchedulingData& data)
{
    auto callback = [=](instruments_strategy_t strategy) {
        chosen_strategy_type = getStrategyIndex(strategy);
        mapping->onRedundancyDecision(data);
    };
    return new function<void(instruments_strategy_t)>(callback);
}

void 
IntNWInstrumentsNetworkChooser::checkRedundancyAsync(CSockMapping *mapping, 
                                                     PendingSenderIROB *psirob, 
                                                     const IROBSchedulingData& data)
{
    auto *pcallback = getRedundancyDecisionCallback(mapping, data);

    wifi_stats->setWifiSessionLengthBound(getCurrentWifiDuration());
    choose_strategy_async(evaluator, (void *) psirob->expected_bytes(), 
                          chosen_strategy_callback_wrapper, pcallback);
}

instruments_strategy_t
IntNWInstrumentsNetworkChooser::getSingularStrategyNotChosen()
{
    assert(NUM_STRATEGIES == 3);
    if (chosen_strategy_type == NETWORK_CHOICE_BOTH) {
        // shouldn't happen; we should only call this when 
        //  preparing to schedule a re-evaluation.
        // if we chose redundancy, we wouldn't be scheduling
        //  a re-evaluation at all.
        assert(false);
        return NULL;
    }
    // return the other strategy - the one not chosen.
    // strategies 0 and 1 are the two singular strategies,
    //  so this picks the other one.
    return strategies[(chosen_strategy_type + 1) % 2];
}

double
IntNWInstrumentsNetworkChooser::getReevaluationDelay(PendingSenderIROB *psirob)
{
    double delay = 0.0;
    instruments_strategy_t chosen_strategy = strategies[chosen_strategy_type];
    instruments_strategy_t not_chosen_strategy = getSingularStrategyNotChosen();

    double best_singular_time = get_last_strategy_time(evaluator, chosen_strategy);
    if (psirob->alreadyReevaluated()) {
        delay = get_last_strategy_time(evaluator, not_chosen_strategy);
        delay -= psirob->getTimeSinceSent();
    } else {
        delay = best_singular_time;

        if (chosen_strategy_type == NETWORK_CHOICE_WIFI) {
            // This only changes the delay if I'm using the weibull distribution
            //  for calculating wifi failure penalty; otherwise the penalty here is zero.
            
            // use transfer time of zero because I'm not storing the actual transfer time;
            //  the failure penalty is baked into the total wifi time.
            // happily, this results in a wifi failure penalty that's slightly smaller,
            //  which means it's still less than the total wifi time.
            delay -= getWifiFailurePenalty(NULL, wifi_stats, 0.0);
        }
        delay *= 2.0;
    }
    
    return max(0.0, delay);
}

void
IntNWInstrumentsNetworkChooser::scheduleReevaluation(CSockMapping *mapping, 
                                                     PendingSenderIROB *psirob,
                                                     const IROBSchedulingData& data)
{
    // if we chose redundancy, we wouldn't be here scheduling a reevaluation
    // and we're holding the lock still, so it won't have been overwritten
    assert(chosen_strategy_type != NETWORK_CHOICE_BOTH);

    InstrumentsWrappedNetStats *chosen_network_stats = 
        (chosen_strategy_type == NETWORK_CHOICE_WIFI
         ? wifi_stats
         : cellular_stats);
    string strategy_name(strategy_names[chosen_strategy_type]);
    double reeval_delay = getReevaluationDelay(psirob);
    irob_id_t id = psirob->get_id();

    // holding scheduling_state_lock
    auto *pcallback_pre_eval = new function<void()>([=]() {
        double min_rtt = psirob->getTimeSinceSent();
        dbgprintf("No ack on %s for IROB %ld; supposing RTT is at least %f seconds and re-evaluating\n",
                  strategy_name.c_str(), id, min_rtt);
        chosen_network_stats->setRttLowerBound(min_rtt);
    });

    auto *pcallback_chosen_strategy = 
        new function<void(instruments_strategy_t)>([=](instruments_strategy_t strategy) {
                auto *inner_cb = getRedundancyDecisionCallback(mapping, data);
                chosen_strategy_callback_wrapper(strategy, inner_cb);
                chosen_network_stats->clearRttLowerBound();
        });
    
    dbgprintf("Scheduling redundancy re-evaluation for IROB %ld in %f seconds\n",
              psirob->get_id(), reeval_delay);

    instruments_scheduled_reevaluation_t reeval = 
        schedule_reevaluation(evaluator, (void *) psirob->expected_bytes(), 
                              pre_eval_callback_wrapper, pcallback_pre_eval,
                              chosen_strategy_callback_wrapper, pcallback_chosen_strategy,
                              reeval_delay);
    psirob->setScheduledReevaluation(reeval);
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
IntNWInstrumentsNetworkChooser::reportNetworkSetup(int network_type)
{
    if (network_type == TYPE_WIFI && wifi_begin.tv_sec == 0) {
        dbgprintf("Beginning new wifi session\n");
        TIME(wifi_begin);
    }
}

void 
IntNWInstrumentsNetworkChooser::reportNetworkTeardown(int network_type)
{
    if (network_type == TYPE_WIFI && wifi_begin.tv_sec != 0) {
        struct timeval end, diff;
        TIME(end);
        TIMEDIFF(wifi_begin, end, diff);
        wifi_begin.tv_sec = wifi_begin.tv_usec = 0;
        dbgprintf("Ending wifi session; was %lu.%06lu seconds\n",
                  diff.tv_sec, diff.tv_usec);
        
        wifi_stats->addSessionDuration(diff);
    }
}

double
IntNWInstrumentsNetworkChooser::getCurrentWifiDuration()
{
    struct timeval now, duration;
    if (wifi_begin.tv_sec != 0) {
        TIME(now);
        TIMEDIFF(wifi_begin, now, duration);
        return duration.tv_sec + (duration.tv_usec / 1000000.0);
    }
    return 0.0;
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
    if (chooser->chosen_strategy_type == -1) {
        dbgprintf("shouldTransmitRedundantly: redundancy decision in progress; non-redundant for now\n");
    } else {
        dbgprintf("shouldTransmitRedundantly: Chosen network strategy: %s\n",
                  strategy_names[chooser->chosen_strategy_type]);
    }
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

    if (shouldSaveStats()) {
        string filename = getSaveStatsFilename();
        saveStats(filename);
    }
}

static const string SESSION_LENGTH_FIELD_NAME = "wifi_session_length";

void 
IntNWInstrumentsNetworkChooser::saveStats(const string& filename)
{
    ofstream out(filename);
    out << SESSION_LENGTH_FIELD_NAME << endl;
    wifi_stats->saveSessionLength(out);
}

void 
IntNWInstrumentsNetworkChooser::loadStats(const string& filename)
{
    ifstream in(filename);
    string name;
    check(in >> name, "Failed to read wifi session length field name");
    check(name == SESSION_LENGTH_FIELD_NAME, "Mismatched wifi session length field name");
    wifi_stats->loadSessionLength(in);
}

bool
IntNWInstrumentsNetworkChooser::shouldLoadStats()
{
    return !getLoadStatsFilename().empty();
}
bool
IntNWInstrumentsNetworkChooser::shouldSaveStats()
{
    return !getSaveStatsFilename().empty();
}

std::string
IntNWInstrumentsNetworkChooser::getLoadStatsFilename()
{
    return Config::getInstance()->getNetworkStatsLoadFilename();
}

std::string
IntNWInstrumentsNetworkChooser::getSaveStatsFilename()
{
    return Config::getInstance()->getNetworkStatsSaveFilename();
}
