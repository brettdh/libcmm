#include "network_chooser.h"
#include "intnw_instruments_network_chooser.h"
#include "intnw_instruments_net_stats_wrapper.h"
#include "libcmm_net_restriction.h"
#include "debug.h"
#include "intnw_config.h"
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

// per-invocation data for network selection.
struct labeled_data {
    u_long send_label;
    int bytelen;
};

static int chooser_arg_less(void *left, void *right)
{
    struct labeled_data *l_data = (struct labeled_data *) left;
    struct labeled_data *r_data = (struct labeled_data *) right;
    return (l_data->send_label < r_data->send_label ||
            (l_data->send_label == r_data->send_label &&
             l_data->bytelen < r_data->bytelen));
    ;
}

static void *chooser_arg_copier(void *arg)
{
    struct labeled_data *source = (struct labeled_data *) arg;
    return new labeled_data{source->send_label, source->bytelen};
}

static void chooser_arg_deleter(void *arg)
{
    struct labeled_data *victim = (struct labeled_data *) arg;
    delete victim;
}

double
network_transfer_time(instruments_context_t ctx, void *strategy_arg, void *chooser_arg)
{
    struct strategy_args *args = (struct strategy_args *)strategy_arg;
    struct labeled_data *data = (struct labeled_data *)chooser_arg;
    ASSERT(args->net_stats);
    return args->chooser->calculateTransferTime(ctx, *args->net_stats, 
                                                data);
}

double
network_transfer_energy_cost(instruments_context_t ctx, void *strategy_arg, void *chooser_arg)
{
    struct strategy_args *args = (struct strategy_args *)strategy_arg;
    struct labeled_data *data = (struct labeled_data *)chooser_arg;
    ASSERT(args->net_stats);

    return args->chooser->calculateTransferEnergy(ctx, *args->net_stats,
                                                  data);
}

double
network_transfer_data_cost(instruments_context_t ctx, void *strategy_arg, void *chooser_arg)
{
    struct strategy_args *args = (struct strategy_args *)strategy_arg;
    struct labeled_data *data = (struct labeled_data *)chooser_arg;
    ASSERT(args->net_stats);
    
    return args->chooser->calculateTransferMobileData(ctx, *args->net_stats, data);
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
                                                      double transfer_time,
                                                      double penalty)
{
    double failure_penalty = 0.0;
    if (net_stats == wifi_stats) {
        double current_wifi_duration = getCurrentWifiDuration();
        failure_penalty = wifi_stats->getWifiFailurePenalty(ctx, transfer_time, 
                                                            current_wifi_duration, penalty);
    }
    return failure_penalty;
}

double
IntNWInstrumentsNetworkChooser::averageWifiFailoverPenalty()
{
    // the time impact of wifi failover 
    //  (A transfer occurs with uniform probability across the entire 
    //   failover window, so it experiences half the max delay on average)
    return  Config::getInstance()->getWifiFailoverDelay() / 2.0;
}


double
IntNWInstrumentsNetworkChooser::calculateTransferTime(instruments_context_t ctx,
                                                      InstrumentsWrappedNetStats *net_stats,
                                                      struct labeled_data *data)
{
    ASSERT(net_stats);
    double bw = getBandwidthUp(ctx, net_stats);
    double rtt_seconds = getRttSeconds(ctx, net_stats);
    u_long send_label = 0;
    int bytelen = 0;
    if (data) {
        send_label = data->send_label;
        bytelen = data->bytelen;
    }
    double tx_time = (bytelen / bw) + rtt_seconds;
    
    double wifi_failure_penalty = 0.0;
    if ((send_label & CMM_LABEL_WIFI_ONLY) == 0) {
        wifi_failure_penalty = getWifiFailurePenalty(ctx, net_stats, tx_time,
                                                     averageWifiFailoverPenalty());
    }

    return tx_time + wifi_failure_penalty;
}

static struct timeval estimate_energy_time = {0, 0};
static int estimate_energy_calls = 0;

static void
reset_estimate_energy_stats()
{
    memset(&estimate_energy_time, 0, sizeof(estimate_energy_time));
    estimate_energy_calls = 0;
}

static void
print_estimate_energy_stats()
{
    dbgprintf("estimate_energy_cost: %d calls, %lu.%06lu seconds\n",
              estimate_energy_calls,
              estimate_energy_time.tv_sec, estimate_energy_time.tv_usec);
}

static inline int
time_estimate_energy_cost(EnergyComputer& calculator, size_t datalen, 
                         size_t bandwidth, size_t rtt_ms)
{
    struct timeval begin, end, diff;
    TIME(begin);
    int cost = calculator(datalen, bandwidth, rtt_ms);
    TIME(end);
    TIMEDIFF(begin, end, diff);
    timeradd(&estimate_energy_time, &diff, &estimate_energy_time);
    estimate_energy_calls++;
    return cost;
}

void
IntNWInstrumentsNetworkChooser::refreshEnergyCalculators()
{
    cellular_energy_calculator = get_energy_computer(TYPE_MOBILE);
    wifi_energy_calculator = get_energy_computer(TYPE_WIFI);
}

double
IntNWInstrumentsNetworkChooser::calculateTransferEnergy(instruments_context_t ctx, 
                                                        InstrumentsWrappedNetStats *net_stats,
                                                        struct labeled_data *data)
{
    ASSERT(net_stats);

    NetworkType type;
    if (net_stats == wifi_stats) {
        type = TYPE_WIFI;
    } else if (net_stats == cellular_stats) {
        type = TYPE_MOBILE;
    } else ASSERT(0);

    EnergyComputer& energy_calculator = (type == TYPE_WIFI) ? wifi_energy_calculator : cellular_energy_calculator;
    
    ASSERT(this);
    ASSERT(ctx);
    ASSERT(net_stats);
    double bw = getBandwidthUp(ctx, net_stats);
    double rtt_seconds = getRttSeconds(ctx, net_stats);
    u_long send_label = 0;
    int bytelen = 0;
    if (data) {
        send_label = data->send_label;
        bytelen = data->bytelen;
    }
    double tx_time = (bytelen / bw) + rtt_seconds;

    // Calculate the expected energy cost due to having to retransmit on cellular if wifi fails.
    //    (cellular_energy * P(wifi fails before transfer completes)
    // Don't bother using the error-adjusted estimates here; great precision is not necessary.
    //    (passing NULL to the get_estimator_value function just returns the estimator value)
    // The reason is that the tail energy will dominate here if the 3G radio
    // isn't active, and if it was activated recently, the energy will be small.
    double wifi_failure_penalty = 0.0;
    if (type == TYPE_WIFI && (send_label & CMM_LABEL_WIFI_ONLY) == 0) {
        double cellular_bw = cellular_stats->get_bandwidth_up(NULL);
        double cellular_rtt_seconds = cellular_stats->get_rtt(NULL);
        double cellular_fallback_energy = time_estimate_energy_cost(cellular_energy_calculator, bytelen, 
                                                                    cellular_bw, cellular_rtt_seconds * 1000.0);
        wifi_failure_penalty = getWifiFailurePenalty(ctx, net_stats, tx_time,
                                                     cellular_fallback_energy);
    }
    
    return time_estimate_energy_cost(energy_calculator, bytelen, bw, rtt_seconds * 1000.0) + wifi_failure_penalty;
}

double
IntNWInstrumentsNetworkChooser::calculateTransferMobileData(instruments_context_t ctx,
                                                            InstrumentsWrappedNetStats *net_stats,
                                                            struct labeled_data *data)
{
    u_long send_label = 0;
    int bytelen = 0;
    if (data) {
        send_label = data->send_label;
        bytelen = data->bytelen;
    }
    if (net_stats == wifi_stats) {
        if (send_label & CMM_LABEL_WIFI_ONLY) {
            return 0.0;
        }

        double bw = getBandwidthUp(ctx, net_stats);
        double rtt_seconds = getRttSeconds(ctx, net_stats);
        double tx_time = (bytelen / bw) + rtt_seconds;
        
        // the penalty is having to send the bytes over cellular anyway if the wifi fails.
        return getWifiFailurePenalty(ctx, net_stats, tx_time, 
                                     (double) bytelen);
    } else if (net_stats == cellular_stats) {
        return bytelen;
    } else ASSERT(0);
}

IntNWInstrumentsNetworkChooser::IntNWInstrumentsNetworkChooser()
    : wifi_present(false), cellular_present(false),
      needs_reevaluation(true), chosen_strategy_type(-1), chosen_singular_strategy_type(-1)
{
    dbgprintf("creating InstrumentsNetworkChooser %p\n", this);

    cellular_stats = new InstrumentsWrappedNetStats("cellular", this);
    wifi_stats = new InstrumentsWrappedNetStats("wifi", this);
    // TODO: sensible (or historical) initial value for wifi session length

    wifi_begin.tv_sec = wifi_begin.tv_usec = 0;

    for (int i = 0; i < NUM_STRATEGIES - 1; ++i) {
        strategy_args[i] = new struct strategy_args;
        strategy_args[i]->chooser = this;
    }
    strategy_args[NETWORK_CHOICE_WIFI]->net_stats = &wifi_stats;
    strategy_args[NETWORK_CHOICE_CELLULAR]->net_stats = &cellular_stats;

    refreshEnergyCalculators();
    for (int i = NETWORK_CHOICE_WIFI; i <= NETWORK_CHOICE_CELLULAR; ++i) {
        strategies[i] = 
            make_strategy(network_transfer_time, 
                          network_transfer_energy_cost,
                          network_transfer_data_cost, 
                          (void *)strategy_args[i], nullptr);
        set_strategy_name(strategies[i], strategy_names[i]);
    }

    strategies[NETWORK_CHOICE_BOTH] = make_redundant_strategy(strategies, 2);
    set_strategy_name(strategies[NETWORK_CHOICE_BOTH], strategy_names[NETWORK_CHOICE_BOTH]);

    EvalMethod method = Config::getInstance()->getEstimatorErrorEvalMethod();
    struct instruments_chooser_arg_fns chooser_arg_fns = {
        chooser_arg_less, chooser_arg_copier, chooser_arg_deleter
    };
    evaluator = register_strategy_set_with_method_and_fns("IntNW", strategies, NUM_STRATEGIES, method,
                                                          chooser_arg_fns);

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

    ASSERT(has_match);
    ASSERT(wifi_present || cellular_present);

    if (!wifi_present || !cellular_present ||
        has_network_restriction(send_label)) {
        bool success = label_matcher.choose_networks(send_label, num_bytes,
                                                     local_iface, remote_iface);
        if (success) {
            if (!wifi_present || send_label & CMM_LABEL_THREEG_ONLY) {
                chosen_singular_strategy_type = NETWORK_CHOICE_CELLULAR;
                chosen_strategy_type = NETWORK_CHOICE_CELLULAR;
            } else if (!cellular_present || send_label & CMM_LABEL_WIFI_ONLY) {
                chosen_singular_strategy_type = NETWORK_CHOICE_WIFI;
                chosen_strategy_type = NETWORK_CHOICE_WIFI;
            }
        }
        return success;
    }
    
    if (needs_reevaluation) {
        dbgprintf("About to choose a network; current wifi session length: %f seconds\n",
                  getCurrentWifiDuration());
        struct timeval begin, end, duration;
        gettimeofday(&begin, NULL);
        chosen_singular_strategy_type = chooseNetwork(send_label, num_bytes);
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
        ASSERT(chosen_singular_strategy_type == NETWORK_CHOICE_CELLULAR);
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
        cellular_present = true;
        cellular_local = local_iface;
        cellular_remote = remote_iface;
    } else ASSERT(false);
}

void
IntNWInstrumentsNetworkChooser::reset()
{
    NetworkChooserImpl::reset();
    wifi_present = false;
    cellular_present = false;
    needs_reevaluation = true;
    chosen_strategy_type = -1;
    chosen_singular_strategy_type = -1;

    label_matcher.reset();
}

int 
IntNWInstrumentsNetworkChooser::chooseNetwork(u_long send_label, int bytelen)
{
    wifi_stats->setWifiSessionLengthBound(getCurrentWifiDuration());

    reset_estimate_energy_stats();
    refreshEnergyCalculators();
    
    struct labeled_data data{send_label, bytelen};
    instruments_strategy_t chosen = choose_nonredundant_strategy(evaluator, &data);
    print_estimate_energy_stats();
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
                                                              IROBSchedulingData data,
                                                              int singular_type)
{
    auto callback = [=](instruments_strategy_t strategy) {
        int type = getStrategyIndex(strategy);
        chosen_strategy_type = type;
        mapping->onRedundancyDecision(data, singular_type, type);
    };
    return new function<void(instruments_strategy_t)>(callback);
}

void 
IntNWInstrumentsNetworkChooser::checkRedundancyAsync(CSockMapping *mapping, 
                                                     PendingSenderIROB *psirob, 
                                                     const IROBSchedulingData& data)
{
    if (Config::getInstance()->getEstimatorErrorEvalMethod() == TRUSTED_ORACLE) {
        // no redundancy
        return;
    }
    
    auto *pcallback = getRedundancyDecisionCallback(mapping, data, chosen_singular_strategy_type);

    wifi_stats->setWifiSessionLengthBound(getCurrentWifiDuration());
    refreshEnergyCalculators();
    struct labeled_data chooser_data{psirob->get_send_labels(), (int) psirob->expected_bytes()};
    choose_strategy_async(evaluator, (void *) &chooser_data, 
                          chosen_strategy_callback_wrapper, pcallback);
}

instruments_strategy_t
IntNWInstrumentsNetworkChooser::getSingularStrategyNotChosen()
{
    ASSERT(NUM_STRATEGIES == 3);
    if (chosen_strategy_type == NETWORK_CHOICE_BOTH) {
        // shouldn't happen; we should only call this when 
        //  preparing to schedule a re-evaluation.
        // if we chose redundancy, we wouldn't be scheduling
        //  a re-evaluation at all.
        ASSERT(false);
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

    if (psirob->alreadyReevaluated()) {
        // 200ms is the default minimum TCP retransmission timeout.
        // At this point, we've fallen back to periodic re-evaluation.
        // This is still better than the old trouble mode, because
        //  we're not just assuming that the network is bad after this delay;
        //  we're checking again by doing our conditional probability calculation.
        // Also, we've already waited twice the time we expected it to take
        //  using our best network, so now we're just trying to find the tipping point
        //  where redundancy wins.  This is much simpler than calculating that point
        //  directly, and it's also independent of the eval method we're using.
        // I thought about using some multiple of the expected redundant-strategy
        //  time, but that just feels like rubbish, since it's definitely
        //  less than the time we've already waited.
        const double periodic_reeval_timeout = 0.200;
        delay = periodic_reeval_timeout;
    } else {
        instruments_strategy_t chosen_strategy = strategies[chosen_strategy_type];
        double best_singular_time = get_last_strategy_time(evaluator, chosen_strategy);
        delay = best_singular_time;

        if (chosen_strategy_type == NETWORK_CHOICE_WIFI) {
            // This only changes the delay if I'm using the weibull distribution
            //  for calculating wifi failure penalty; otherwise the penalty here is zero.
            
            // use transfer time of zero because I'm not storing the actual transfer time;
            //  the failure penalty is baked into the total wifi time.
            // happily, this results in a wifi failure penalty that's slightly smaller,
            //  which means it's still less than the total wifi time.
            delay -= getWifiFailurePenalty(NULL, wifi_stats, 0.0, averageWifiFailoverPenalty());
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
    ASSERT(chosen_strategy_type != NETWORK_CHOICE_BOTH);

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
                auto *inner_cb = getRedundancyDecisionCallback(mapping, data, chosen_singular_strategy_type);
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
    ASSERT(0);
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
        ASSERT(false);
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
    dbgprintf_always("%s", oss.str().c_str());
    
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
        
        unlock();
        // updating estimators grabs a lock on each subscriber to each estimator,
        // which can result in deadlock, so drop the eval lock first.

        wifi_stats->addSessionDuration(diff);

        wifi_stats->resetError();
        lock();
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

instruments_strategy_t
IntNWInstrumentsNetworkChooser::getChosenStrategy(u_long net_restriction_labels)
{
    assert(!(net_restriction_labels & CMM_LABEL_WIFI_ONLY) ||
           !(net_restriction_labels & CMM_LABEL_THREEG_ONLY));
    if (net_restriction_labels & CMM_LABEL_WIFI_ONLY) {
        return strategies[NETWORK_CHOICE_WIFI];
    } else if (net_restriction_labels & CMM_LABEL_THREEG_ONLY) {
        return strategies[NETWORK_CHOICE_CELLULAR];
    }

    // we're holding this chooser's lock (only accessible via GuardedNetworkChooser
    if (chosen_strategy_type != -1) {
        return strategies[chosen_strategy_type];
    } else if (chosen_singular_strategy_type != -1) {
        return strategies[chosen_singular_strategy_type];
    } else {
        // this shouldn't happen, because IntNW should choose a strategy
        //  at the beginning, and after that, reset + re-choose is atomic.
        dbgprintf("WARNING! BAD! IntNW hasn't chosen a strategy yet.\n");
        ASSERT(false);
        return nullptr;
    }
}

double
IntNWInstrumentsNetworkChooser::getEstimatedTransferTime(instruments_context_t context, 
                                                         instruments_strategy_t strategy,
                                                         u_long send_label,
                                                         size_t bytes)
{
    struct labeled_data data{send_label, (int) bytes};
    return calculate_strategy_time(context, strategy, (void *) &data);
}

double
IntNWInstrumentsNetworkChooser::getEstimatedTransferEnergy(instruments_context_t context, 
                                                           instruments_strategy_t strategy,
                                                           u_long send_label,
                                                           size_t bytes)
{
    struct labeled_data data{send_label, (int) bytes};
    return calculate_strategy_energy(context, strategy, (void *) &data);
}

double
IntNWInstrumentsNetworkChooser::getEstimatedTransferData(instruments_context_t context, 
                                                         instruments_strategy_t strategy,
                                                         u_long send_label,
                                                         size_t bytes)
{
    struct labeled_data data{send_label, (int) bytes};
    return calculate_strategy_data(context, strategy, (void *) &data);
}

void
IntNWInstrumentsNetworkChooser::setRedundancyStrategy()
{
    dbgprintf("setting IntNWInstrumentsNetworkChooser::RedundancyStrategy\n");
    ASSERT(redundancyStrategy == NULL);
    redundancyStrategy = 
        new IntNWInstrumentsNetworkChooser::RedundancyStrategy(this);
}

IntNWInstrumentsNetworkChooser::RedundancyStrategy::
RedundancyStrategy(IntNWInstrumentsNetworkChooser *chooser_)
    : chooser(chooser_)
{
}

bool
is_redundant(int chosen_singular_strategy_type,
             int chosen_strategy_type)
{
    return (chosen_strategy_type != -1 &&
            (chosen_strategy_type == NETWORK_CHOICE_BOTH ||
             chosen_singular_strategy_type != chosen_strategy_type));
    // XXX: this is kind of a hack, since in the current world,
    // XXX: there are only ever two network interfaces, and
    // XXX: therefore changing the decision from one to the other
    // XXX: is equivalent to choosing both in the first place.
}

bool 
IntNWInstrumentsNetworkChooser::RedundancyStrategy::
shouldTransmitRedundantly(PendingSenderIROB *psirob)
{
    if (Config::getInstance()->getEstimatorErrorEvalMethod() == TRUSTED_ORACLE) {
        return false;
    }

    ASSERT(chooser->has_match);
    if (chooser->chosen_strategy_type == -1) {
        dbgprintf("shouldTransmitRedundantly: redundancy decision in progress; non-redundant for now\n");
    } else {
        dbgprintf("shouldTransmitRedundantly: Chosen network strategy: %s\n",
                  strategy_names[chooser->chosen_strategy_type]);
    }
    // either the async evaluation decided on redundancy, 
    // or I'm doing a deferred re-evaluation, and the passage of time
    // changed the decision.
    return is_redundant(chooser->chosen_singular_strategy_type,
                        chooser->chosen_strategy_type);
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

instruments_estimator_t 
IntNWInstrumentsNetworkChooser::get_rtt_estimator(u_long net_restriction_labels)
{
    if (net_restriction_labels & CMM_LABEL_WIFI_ONLY) {
        return wifi_stats->getRttEstimator();
    } else if (net_restriction_labels & CMM_LABEL_THREEG_ONLY) {
        return cellular_stats->getRttEstimator();
    }
    return nullptr;
}
