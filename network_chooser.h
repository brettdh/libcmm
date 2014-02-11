#ifndef _NETWORK_CHOOSER_H_INCLUDED_AV9G84UBOV
#define _NETWORK_CHOOSER_H_INCLUDED_AV9G84UBOV

#include <sys/types.h>
#include <pthread.h>
#include <map>
#include "net_interface.h"
#include "libcmm.h"
#include "csocket.h"

#include <instruments.h>

class PendingSenderIROB;
class RedundancyStrategy;
class NetworkChooserImpl;
class CSockMapping;

// reset + consider + choose_networks must be atomic,
//  so they are only available through this interface.
// call network_chooser->getGuardedChooser(),
//  then call these methods on the returned object.
// The guarded network chooser grabs the lock on construction
//  and releases it on destruction.
class NetworkChooserGuard {
  public:
    void reset();
    
    // for use with CSockMapping::for_each
    void consider(CSocketPtr csock) {
        consider(csock->local_iface, csock->remote_iface);
    }
    
    void consider(struct net_interface local_iface, 
                  struct net_interface remote_iface);
    
    bool choose_networks(u_long send_label,
                         struct net_interface& local_iface,
                         struct net_interface& remote_iface);
    
    // if not available, num_bytes == 0.
    bool choose_networks(u_long send_label, size_t num_bytes,
                         struct net_interface& local_iface,
                         struct net_interface& remote_iface);

    
    instruments_strategy_t getChosenStrategy(u_long net_restriction_labels=0);
    
    double getEstimatedTransferTime(instruments_context_t context, 
                                    instruments_strategy_t strategy,
                                    u_long send_label,
                                    size_t bytes);
    double getEstimatedTransferEnergy(instruments_context_t context, 
                                      instruments_strategy_t strategy,
                                      u_long send_label,
                                      size_t bytes);
    double getEstimatedTransferData(instruments_context_t context, 
                                    instruments_strategy_t strategy,
                                    u_long send_label,
                                    size_t bytes);

    ~NetworkChooserGuard();
  private:
    friend class NetworkChooser;
    
    NetworkChooserGuard(NetworkChooser *chooser);
    NetworkChooser *chooser;
};
typedef std::shared_ptr<NetworkChooserGuard> GuardedNetworkChooser;

class NetworkChooser {
  public:
    static NetworkChooser* create(int redundancy_strategy_type);

    GuardedNetworkChooser getGuardedChooser();
    
    void reportNetStats(int network_type, 
                        double new_bw,
                        double new_bw_estimate,
                        double new_latency_seconds,
                        double new_latency_estimate);

    void reportNetworkSetup(int network_type);
    void reportNetworkTeardown(int network_type);

    bool shouldTransmitRedundantly(PendingSenderIROB *psirob);
    void checkRedundancyAsync(CSockMapping *mapping,
                              PendingSenderIROB *psirob, 
                              const IROBSchedulingData& data);
    void scheduleReevaluation(CSockMapping *mapping,
                              PendingSenderIROB *psirob, 
                              const IROBSchedulingData& data);

    instruments_estimator_t get_rtt_estimator(u_long net_restriction_labels);

    void saveToFile();

    ~NetworkChooser();
  private:
    friend class NetworkChooserGuard;
    friend class NetworkChooserImpl;

    NetworkChooser(NetworkChooserImpl *pimpl_);
    NetworkChooserImpl *impl;
    pthread_mutex_t lock;
};

#endif /* _NETWORK_CHOOSER_H_INCLUDED_AV9G84UBOV */
