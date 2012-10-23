#ifndef _NETWORK_CHOOSER_H_INCLUDED_AV9G84UBOV
#define _NETWORK_CHOOSER_H_INCLUDED_AV9G84UBOV

#include <sys/types.h>
#include <map>
#include "net_interface.h"
#include "libcmm.h"
#include "csocket.h"

class RedundancyStrategy;

class NetworkChooser {
  public:
    static NetworkChooser* create(int redundancy_strategy_type);

    virtual void reset();
    
    // for use with CSockMapping::for_each
    void consider(CSocketPtr csock) {
        consider(csock->local_iface, csock->remote_iface);
    }
    
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

    RedundancyStrategy *getRedundancyStrategy();
  protected:
    NetworkChooser();

    bool has_match;

    // default: never redundant.
    //   subclasses should override this to replace
    //   the default with a custom redundancy strategy
    virtual void setRedundancyStrategy();
    RedundancyStrategy *redundancyStrategy;
};

class PreferredNetwork : public NetworkChooser {
    int preferred_type;
    bool has_match;
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
class LabelMatcher : public NetworkChooser {
    u_long max_bw;
    u_long min_RTT;
    bool has_match;
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
