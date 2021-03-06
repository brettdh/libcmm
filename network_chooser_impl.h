#ifndef _NETWORK_CHOOSER_IMPL_H_INCLUDED_AV9G84UBOV
#define _NETWORK_CHOOSER_IMPL_H_INCLUDED_AV9G84UBOV

#include <sys/types.h>
#include <map>
#include "net_interface.h"
#include "libcmm.h"
#include "csocket.h"
#include "network_chooser.h"

class RedundancyStrategy;
class CSockMapping;

class NetworkChooserImpl {
  public:
    // reset the state set by consider() to prepare for a 
    //  new round of consider() calls followed by a choose_networks() call.
    // The chooser should NOT reset state that can be maintained
    //  between calls to choose_networks.
    virtual void reset();
    
    virtual void consider(struct net_interface local_iface, 
                          struct net_interface remote_iface) {}
    
    bool choose_networks(u_long send_label,
                         struct net_interface& local_iface,
                         struct net_interface& remote_iface);

    // if not available, num_bytes == 0.
    virtual bool choose_networks(u_long send_label, size_t num_bytes,
                                 struct net_interface& local_iface,
                                 struct net_interface& remote_iface) = 0;
    
    virtual void reportNetStats(int network_type, 
                                double new_bw,
                                double new_bw_estimate,
                                double new_latency_seconds,
                                double new_latency_estimate) {}

    virtual void reportNetworkSetup(int network_type) {}
    virtual void reportNetworkTeardown(int network_type) {}
    
    bool shouldTransmitRedundantly(PendingSenderIROB *psirob);
    virtual void setRedundancyStrategy();

    // override in subclass to do slow redundancy evaluation off the critical path
    // (that is, after doing a cheap single-network calculation
    //  and sending on at least one network)
    virtual void checkRedundancyAsync(CSockMapping *mapping,
                                      PendingSenderIROB *psirob, 
                                      const IROBSchedulingData& data) {}

    // override in subclass to schedule a re-evaluation
    //  (after choosing a non-redundant strategy)
    virtual void scheduleReevaluation(CSockMapping *mapping,
                                      PendingSenderIROB *psirob, 
                                      const IROBSchedulingData& data) {}


    virtual void saveToFile() {}

    // implement if the chooser uses libinstruments.
    virtual instruments_strategy_t getChosenStrategy(u_long net_restriction_labels) { return nullptr; }
    virtual double getEstimatedTransferTime(instruments_context_t context, 
                                            instruments_strategy_t strategy,
                                            u_long send_label,
                                            size_t bytes) { return 0.0; }
    virtual double getEstimatedTransferEnergy(instruments_context_t context, 
                                              instruments_strategy_t strategy,
                                              u_long send_label,
                                              size_t bytes){ return 0.0; }
    virtual double getEstimatedTransferData(instruments_context_t context, 
                                            instruments_strategy_t strategy,
                                            u_long send_label,
                                            size_t bytes) { return 0.0; }

    virtual instruments_estimator_t get_rtt_estimator(u_long net_restriction_labels) {
        return nullptr;
    }

    virtual ~NetworkChooserImpl() {}
    
    void setWrapper(NetworkChooser *wrapper_);

    // for releasing wrapper lock briefly, as needed to avoid deadlock
    void lock();
    void unlock();

  protected:
    NetworkChooserImpl();

    GuardedNetworkChooser getGuardedChooser();

    bool has_match;

    // default: never redundant.
    //   subclasses should override this to replace
    //   the default with a custom redundancy strategy
    RedundancyStrategy *redundancyStrategy;

  private:
    NetworkChooser *wrapper;
};

class PreferredNetwork : public NetworkChooserImpl {
    int preferred_type;
    struct net_interface local, remote;
  public:
    PreferredNetwork(int preferred_type_);
    virtual void consider(struct net_interface local_iface, 
                          struct net_interface remote_iface);
    virtual bool choose_networks(u_long send_label, size_t num_bytes,
                                 struct net_interface& local_iface,
                                 struct net_interface& remote_iface);
};


// Call consider() with several different label pairs, then
//  call pick_label_match to get the local and remote
//  interfaces among the pairs considered that best match
//  the labels.
class LabelMatcher : public NetworkChooserImpl {
    u_long max_bw;
    u_long min_RTT;
    bool has_wifi_match;
    bool has_threeg_match;
    std::pair<struct net_interface, struct net_interface> max_bw_iface_pair;
    std::pair<struct net_interface, struct net_interface> min_RTT_iface_pair;

    std::pair<struct net_interface, struct net_interface> wifi_pair;
    std::pair<struct net_interface, struct net_interface> threeg_pair;

  public:
    virtual void reset() {
        max_bw = 0;
        min_RTT = ULONG_MAX;
        has_match = false;
        has_wifi_match = false;
        has_threeg_match = false;
    }

    // Call this on each pair to be considered
    virtual void consider(struct net_interface local_iface, 
                          struct net_interface remote_iface);
    
    virtual bool choose_networks(u_long send_label, size_t num_bytes,
                                 struct net_interface& local_iface,
                                 struct net_interface& remote_iface);
};


class AlwaysRedundantChooser : public PreferredNetwork {
  public:
    AlwaysRedundantChooser();
  protected:
    virtual void setRedundancyStrategy();
};

#endif /* _NETWORK_CHOOSER_H_INCLUDED_AV9G84UBOV */
