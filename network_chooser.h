#ifndef _NETWORK_CHOOSER_H_INCLUDED_AV9G84UBOV
#define _NETWORK_CHOOSER_H_INCLUDED_AV9G84UBOV

#include <sys/types.h>
#include <pthread.h>
#include <map>
#include "net_interface.h"
#include "libcmm.h"
#include "csocket.h"

class PendingSenderIROB;
class RedundancyStrategy;
class NetworkChooserImpl;

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

    ~NetworkChooserGuard();
  private:
    friend class NetworkChooser;
    
    NetworkChooserGuard(NetworkChooser *chooser);
    NetworkChooser *chooser;
};
typedef boost::shared_ptr<NetworkChooserGuard> GuardedNetworkChooser;

class NetworkChooser {
  public:
    static NetworkChooser* create(int redundancy_strategy_type);

    GuardedNetworkChooser getGuardedChooser();
    
    void reportNetStats(int network_type, 
                        double new_bw,
                        double new_bw_estimate,
                        double new_latency_seconds,
                        double new_latency_estimate);

    bool shouldTransmitRedundantly(PendingSenderIROB *psirob);

    ~NetworkChooser();
  private:
    friend class NetworkChooserGuard;

    NetworkChooser(NetworkChooserImpl *pimpl_);
    NetworkChooserImpl *impl;
    pthread_mutex_t lock;
};

#endif /* _NETWORK_CHOOSER_H_INCLUDED_AV9G84UBOV */
