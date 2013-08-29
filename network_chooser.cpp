#include <sys/types.h>
#include <map>
#include "debug.h"
#include "pthread_util.h"
#include "net_interface.h"
#include "libcmm.h"
#include "csocket.h"
#include "network_chooser.h"
#include "network_chooser_impl.h"
#include "intnw_instruments_network_chooser.h"
#include "libcmm_net_restriction.h"

#include "pending_sender_irob.h"

NetworkChooser* 
NetworkChooser::create(int redundancy_strategy_type)
{
    NetworkChooserImpl *impl = NULL;
    switch (redundancy_strategy_type) {
    case CELLULAR_ONLY:
        impl = new PreferredNetwork(NET_TYPE_THREEG);
        break;
    case WIFI_PREFERRED:
        impl = new PreferredNetwork(NET_TYPE_WIFI);
        break;
    case INTNW_NEVER_REDUNDANT:
        impl = new LabelMatcher;
        break;
    case INTNW_REDUNDANT:
        impl = new IntNWInstrumentsNetworkChooser;
        break;
    case ALWAYS_REDUNDANT:
        impl = new AlwaysRedundantChooser;
        break;
    default:
        ASSERT(0);
    }

    dbgprintf("Constructing NetworkChooserImpl %p\n", impl);
    
    impl->reset();
    impl->setRedundancyStrategy();

    NetworkChooser *wrapper = new NetworkChooser(impl);
    impl->setWrapper(wrapper);
    return wrapper;
}

NetworkChooser::NetworkChooser(NetworkChooserImpl *impl_)
    : impl(impl_)
{
    pthread_mutex_init(&lock, NULL);
}

NetworkChooser::~NetworkChooser()
{
    pthread_mutex_destroy(&lock);
    delete impl;
}

GuardedNetworkChooser
NetworkChooser::getGuardedChooser()
{
    GuardedNetworkChooser guard(new NetworkChooserGuard(this));
    return guard;
}

NetworkChooserGuard::NetworkChooserGuard(NetworkChooser *chooser_)
    : chooser(chooser_)
{
    pthread_mutex_lock(&chooser->lock);
}

NetworkChooserGuard::~NetworkChooserGuard()
{
    pthread_mutex_unlock(&chooser->lock);
}

void 
NetworkChooserGuard::reset()
{
    chooser->impl->reset();
}

void 
NetworkChooserGuard::consider(struct net_interface local_iface, 
                              struct net_interface remote_iface)
{
    chooser->impl->consider(local_iface, remote_iface);
}
    
bool 
NetworkChooserGuard::choose_networks(u_long send_label,
                                     struct net_interface& local_iface,
                                     struct net_interface& remote_iface)
{
    return chooser->impl->choose_networks(send_label, local_iface, remote_iface);
}

// if not available, num_bytes == 0.
bool 
NetworkChooserGuard::choose_networks(u_long send_label, size_t num_bytes,
                                     struct net_interface& local_iface,
                                     struct net_interface& remote_iface)
{
    bool result = chooser->impl->choose_networks(send_label, num_bytes,
                                                 local_iface, remote_iface);

    StringifyIP local_ip(&local_iface.ip_addr);
    StringifyIP remote_ip(&remote_iface.ip_addr);

    dbgprintf("Decided to send %d bytes over connection %s -> %s\n",
              num_bytes, local_ip.c_str(), remote_ip.c_str());
    return result;
}

void 
NetworkChooser::reportNetStats(int network_type,
                               double new_bw,
                               double new_bw_estimate,
                               double new_latency_seconds,
                               double new_latency_estimate)
{
    PthreadScopedLock guard(&lock);
    impl->reportNetStats(network_type, new_bw, new_bw_estimate,
                         new_latency_seconds, new_latency_estimate);
}

void
NetworkChooser::reportNetworkSetup(int network_type)
{
    PthreadScopedLock guard(&lock);
    impl->reportNetworkSetup(network_type);
}

void
NetworkChooser::reportNetworkTeardown(int network_type)
{
    PthreadScopedLock guard(&lock);
    impl->reportNetworkTeardown(network_type);
}

bool
NetworkChooser::shouldTransmitRedundantly(PendingSenderIROB *psirob)
{
    PthreadScopedLock guard(&lock);
    bool redundant = impl->shouldTransmitRedundantly(psirob);
    return redundant;
}


void 
NetworkChooser::checkRedundancyAsync(CSockMapping *mapping,
                                     PendingSenderIROB *psirob, 
                                     const IROBSchedulingData& data)
{
    PthreadScopedLock guard(&lock);
    impl->checkRedundancyAsync(mapping, psirob, data);
}

void 
NetworkChooser::scheduleReevaluation(CSockMapping *mapping,
                                     PendingSenderIROB *psirob, 
                                     const IROBSchedulingData& data)
{
    PthreadScopedLock guard(&lock);
    impl->scheduleReevaluation(mapping, psirob, data);
}

void
NetworkChooser::saveToFile()
{
    PthreadScopedLock guard(&lock);
    impl->saveToFile();
}
