#include "network_chooser_impl.h"
#include "intnw_instruments_network_chooser.h"
#include "libcmm_net_restriction.h"
#include "debug.h"

#include "redundancy_strategy.h"

#include <map>
using std::make_pair;

NetworkChooserImpl::NetworkChooserImpl()
    : redundancyStrategy(NULL), wrapper(NULL)
{
}

void 
NetworkChooserImpl::setWrapper(NetworkChooser *wrapper_)
{
    wrapper = wrapper_;
}

GuardedNetworkChooser
NetworkChooserImpl::getGuardedChooser()
{
    ASSERT(wrapper);
    return wrapper->getGuardedChooser();
}

void
NetworkChooserImpl::setRedundancyStrategy()
{ 
    dbgprintf("In NetworkChooserImpl::setRedundancyStrategy\n");

    ASSERT(redundancyStrategy == NULL);
    redundancyStrategy = RedundancyStrategy::create(INTNW_NEVER_REDUNDANT);
}

bool
NetworkChooserImpl::shouldTransmitRedundantly(PendingSenderIROB *psirob)
{
    ASSERT(redundancyStrategy != NULL);
    return redundancyStrategy->shouldTransmitRedundantly(psirob);
}

void
NetworkChooserImpl::reset()
{
    has_match = false;
}

bool 
NetworkChooserImpl::choose_networks(u_long send_label,
                                    struct net_interface& local_iface,
                                    struct net_interface& remote_iface)
{
    return choose_networks(send_label, 0, local_iface, remote_iface);
}

void 
NetworkChooserImpl::lock()
{
    int rc = pthread_mutex_lock(&wrapper->lock);
    PTHREAD_ASSERT_SUCCESS(rc);
}

void 
NetworkChooserImpl::unlock()
{
    int rc = pthread_mutex_unlock(&wrapper->lock);
    PTHREAD_ASSERT_SUCCESS(rc);
}


PreferredNetwork::PreferredNetwork(int preferred_type_)
{
    preferred_type = preferred_type_;
}

void
PreferredNetwork::consider(struct net_interface local_iface, 
                           struct net_interface remote_iface)
{
    if (!has_match || matches_type(preferred_type, local_iface, remote_iface)) {
        has_match = true;
        local = local_iface;
        remote = remote_iface;
    }
}

bool 
PreferredNetwork::choose_networks(u_long send_label, size_t num_bytes,
                                  struct net_interface& local_iface,
                                  struct net_interface& remote_iface)
{
    if (!has_match) {
        return false;
    }
    local_iface = local;
    remote_iface = remote;
    return true;
}

void 
LabelMatcher::consider(struct net_interface local_iface, 
                       struct net_interface remote_iface)
{
    u_long bw, RTT;
    if (!NetStats::get_estimate(local_iface, remote_iface, NET_STATS_BW_UP, bw)) {
        bw = iface_bandwidth(local_iface, remote_iface);
    }
    if (iface_bandwidth(local_iface, remote_iface) == 0) {
        // special-case this, since the estimate won't 
        // ever reflect when this happens
        bw = 0;
    }

    if (!NetStats::get_estimate(local_iface, remote_iface, NET_STATS_LATENCY, RTT)) {
        RTT = iface_RTT(local_iface, remote_iface);
    } else {
        RTT = RTT * 2;
    }
        
    if (bw > max_bw) {
        max_bw = bw;
        max_bw_iface_pair = make_pair(local_iface, remote_iface);
    }
    if (RTT < min_RTT) {
        min_RTT = RTT;
        min_RTT_iface_pair = make_pair(local_iface, remote_iface);
    }

    // we have a match after we've considered at least one.
    has_match = true;

    if (matches_type(NET_TYPE_WIFI, local_iface, remote_iface)) {
        wifi_pair = make_pair(local_iface, remote_iface);
        has_wifi_match = true;
    }
    if (matches_type(NET_TYPE_THREEG, local_iface, remote_iface)) {
        threeg_pair = make_pair(local_iface, remote_iface);
        has_threeg_match = true;
    }
}

bool 
LabelMatcher::choose_networks(u_long send_label, size_t num_bytes,
                              struct net_interface& local_iface,
                              struct net_interface& remote_iface)
{
    if (!has_match) {
        return false;
    }

    // first, check net type restriction labels, since they take precedence
    if (send_label & CMM_LABEL_WIFI_ONLY) {
        if (!has_wifi_match) {
            if (fallback_allowed(send_label) && has_threeg_match) {
                // XXX: should fall back on ANY network, or choose from among the rest
                // XXX: (but there's only one other network, most of the time)
                local_iface = threeg_pair.first;
                remote_iface = threeg_pair.second;
                return true;
            } else {
                return false;
            }
        }

        local_iface = wifi_pair.first;
        remote_iface = wifi_pair.second;
        return true;
    } else if (send_label & CMM_LABEL_THREEG_ONLY) {
        if (!has_threeg_match) {
            if (fallback_allowed(send_label) && has_wifi_match) {
                // XXX: should fall back on ANY network, or choose from among the rest
                // XXX: (but there's only one other network, most of the time)
                local_iface = wifi_pair.first;
                remote_iface = wifi_pair.second;
                return true;
            } else {
                return false;
            }
        }

        local_iface = threeg_pair.first;
        remote_iface = threeg_pair.second;
        return true;
    }
    // else: no net type restriction; carry on with other label matching

    const u_long LABELMASK_FGBG = CMM_LABEL_ONDEMAND | CMM_LABEL_BACKGROUND;

    // TODO: try to check based on the actual size
    if (send_label & CMM_LABEL_SMALL &&
        min_RTT < ULONG_MAX) {
        local_iface = min_RTT_iface_pair.first;
        remote_iface = min_RTT_iface_pair.second;
        return true;
    } else if (send_label & CMM_LABEL_LARGE) {
        local_iface = max_bw_iface_pair.first;
        remote_iface = max_bw_iface_pair.second;
        return true;
    } else if (send_label & CMM_LABEL_ONDEMAND ||
               !(send_label & LABELMASK_FGBG)) {
        local_iface = min_RTT_iface_pair.first;
        remote_iface = min_RTT_iface_pair.second;
        return true;            
    } else if (send_label & CMM_LABEL_BACKGROUND ||
               send_label == 0) {
        local_iface = max_bw_iface_pair.first;
        remote_iface = max_bw_iface_pair.second;
        return true;            
    } else {
        local_iface = max_bw_iface_pair.first;
        remote_iface = max_bw_iface_pair.second;
        return true;
    }
    return false;
}


AlwaysRedundantChooser::AlwaysRedundantChooser()
    : PreferredNetwork(NET_TYPE_WIFI)
{
}

void
AlwaysRedundantChooser::setRedundancyStrategy()
{
    ASSERT(redundancyStrategy == NULL);
    redundancyStrategy = RedundancyStrategy::create(ALWAYS_REDUNDANT);
}
