#include "csocket_mapping.h"
#include "csocket.h"
#include "csocket_sender.h"
#include "csocket_receiver.h"
#include "cmm_conn_bootstrapper.h"
#include "pending_sender_irob.h"
#include "debug.h"
#include <memory>
#include <map>
#include <vector>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <functional>
using std::bind2nd;
using std::ptr_fun;
using std::make_pair;

#include "cmm_socket.private.h"
#include "libcmm.h"
#include "libcmm_net_restriction.h"

#include "redundancy_strategy.h"
#include "network_chooser.h"
#include "irob_scheduling.h"

#include "config.h"

using std::unique_ptr;
using std::pair;
using std::vector;
using std::map;

#include "pthread_util.h"

/* TODO-REDUNDANCY: here's the big idea.
 *
 * Checking whether a csocket was troubled was the way to decide whether
 *   to transmit redundantly.
 * The basic rule here is that I should probably be deciding somewhere above here
 *   whether to transmit redundantly, and then I won't even need to 
 *   call into here to choose a csocket; or else I'll use a different call
 *   that gives me ALL csockets, because I want to transmit redundantly.
 */
    
CSockMapping::CSockMapping(CMMSocketImplPtr sk_)
    : sk(sk_)
{
    RWLOCK_INIT(&sockset_mutex, NULL);
    network_chooser = NULL;
    set_redundancy_strategy(INTNW_NEVER_REDUNDANT);
}

CSockMapping::~CSockMapping()
{
    PthreadScopedRWLock lock(&sockset_mutex, true);
    available_csocks.clear();
    
    delete network_chooser;
}

void
CSockMapping::set_redundancy_strategy(int type)
{
    delete network_chooser;
    network_chooser = NetworkChooser::create(type);

    redundancy_strategy_type = type;
}

int 
CSockMapping::get_redundancy_strategy()
{
    assert(network_chooser);
    return redundancy_strategy_type;
}

NetworkChooser *
CSockMapping::get_network_chooser()
{
    return network_chooser;
}

size_t
CSockMapping::count()
{
    PthreadScopedRWLock lock(&sockset_mutex, false);
    return count_locked();
}

size_t
CSockMapping::count_locked()
{
    return available_csocks.size();
}

size_t 
CSockMapping::count_connected()
{
    PthreadScopedRWLock lock(&sockset_mutex, false);
    return count_connected_locked();
}

size_t 
CSockMapping::count_connected_locked()
{
    size_t num_connected_csocks = 0;
    for (CSockSet::iterator it = available_csocks.begin();
         it != available_csocks.end(); it++) {
        CSocketPtr csock = *it;
        if (csock->is_connected()) {
            ++num_connected_csocks;
        }
    }
    return num_connected_csocks;
}

bool
CSockMapping::empty()
{
    PthreadScopedRWLock lock(&sockset_mutex, false);
    return available_csocks.empty();
}

struct push_osfd {
    mc_socket_t mc_sock;
    mcSocketOsfdPairList &osfd_list;
    push_osfd(mc_socket_t mcs, mcSocketOsfdPairList& list) 
        : mc_sock(mcs), osfd_list(list) {}
    int operator()(CSocketPtr csock) {
        ASSERT(csock);
        int osfd = csock->osfd;
        ASSERT(osfd > 0);

        osfd_list.push_back(pair<mc_socket_t,int>(mc_sock,osfd));
        return 0;
    }
};

void
CSockMapping::get_real_fds(mcSocketOsfdPairList &osfd_list)
{
    push_osfd pusher(CMMSocketImplPtr(sk)->sock, osfd_list);
    (void)for_each(pusher);
}

struct get_matching_csocks {
    const struct net_interface *local_iface;
    const struct net_interface *remote_iface;
    vector<CSocketPtr>& matches;
    bool local;
    get_matching_csocks(const struct net_interface *local_iface_,
                        const struct net_interface *remote_iface_,
                        vector<CSocketPtr>& matches_)
        : local_iface(local_iface_), remote_iface(remote_iface_), 
          matches(matches_) {}

    bool iface_matches(const struct net_interface *ref, 
                       const struct net_interface *candidate) {
        return (!ref || ref->ip_addr.s_addr == candidate->ip_addr.s_addr);
    }

    int operator()(CSocketPtr csock) {
        ASSERT(csock);

        if (iface_matches(local_iface, &csock->local_iface) &&
            iface_matches(remote_iface, &csock->remote_iface)) {
            matches.push_back(csock);
        }
        return 0;
    }
};

/* already holding sk->my_lock, writer=true */
void 
CSockMapping::setup(struct net_interface iface, bool local,
                    bool make_connection, bool need_data_check)
{
    vector<CSocketPtr> matches;
    struct net_interface *local_iface = local ? &iface : NULL;
    struct net_interface *remote_iface = local ? NULL : &iface;

    get_matching_csocks getter(local_iface, remote_iface, matches);
    (void)for_each(getter);

    for (size_t i = 0; i < matches.size(); ++i) {
        // replace connection stats with updated numbers
        PthreadScopedLock lock(&matches[i]->csock_lock);
        if (local) {
            matches[i]->local_iface = iface;
        } else {
            matches[i]->remote_iface = iface;
        }
        matches[i]->stats.update(matches[i]->local_iface,
                                 matches[i]->remote_iface,
                                 network_chooser, matches[i]->network_type());
    }

    CMMSocketImplPtr skp(sk);
    if (!make_connection || skp->accepting_side) {
        // bootstrapping in progress or failed; don't try to 
        // start up a new connection
        // also skip this step if we're the accepting side;
        //  only the connecting side will make new connections.
        //  This avoids NAT difficulties.
        return;
    }

    // Let's try making a CSocket whenever a network pair becomes available.
    //  That way, it will be available for striping, even if I haven't 
    //  asked for labels that create CSockets on all networks.
    NetInterfaceSet::iterator it, end;
    if (local) { // try this out to avoid some of the contention.
        it = skp->remote_ifaces.begin();
        end = skp->remote_ifaces.end();
        for (; it != end; ++it) {
            struct net_interface local_iface, remote_iface;
            local_iface = iface;
            remote_iface = *it;
            if (need_data_check || !csock_by_ifaces(local_iface, remote_iface)) {
                (void) make_new_csocket(local_iface, remote_iface);
            }
        }
    }
}

struct WaitForConnection {
    int num_connections;
    WaitForConnection() : num_connections(0) {}
    int operator()(CSocketPtr csock) {
        int rc = csock->wait_until_connected();
        if (rc < 0) {
            // print, but don't fail the for_each
            dbgprintf("CSocket %d failed to connect on bootstrapping\n",
                      csock->osfd);
            
        } else {
            num_connections++;
        }
        return 0;
    }
};

void
CSockMapping::wait_for_connections()
{
    // only called on bootstrapping.
    WaitForConnection obj;
    for_each(obj);
    if (obj.num_connections == 0) {
        dbgprintf("All connection attempts on bootstrap failed!\n");
        throw -1;
    }
}

void
CSockMapping::teardown(struct net_interface iface, bool local)
{
    vector<CSocketPtr> victims;
    struct net_interface *local_iface = local ? &iface : NULL;
    struct net_interface *remote_iface = local ? NULL : &iface;
    get_matching_csocks getter(local_iface, remote_iface, victims);
    (void)for_each(getter);

    PthreadScopedRWLock lock(&sockset_mutex, true);
    while (!victims.empty()) {
        CSocketPtr victim = victims.back();
        victims.pop_back();
        available_csocks.erase(victim);

        dbgprintf("Tearing down CSocket %d (%s interface %s is gone\n",
                  victim->osfd, local ? "local" : "remote", StringifyIP(&iface.ip_addr).c_str());
        shutdown(victim->osfd, SHUT_RDWR); /* tells the sender/receiver threads to exit */
    }
}

// function operator returns true iff this CSocket is the
//  best match for the labels, given all the available interfaces.
struct GlobalLabelMatch {
    CSockMapping *cskmap;
    u_long send_label;
    size_t num_bytes;

    GlobalLabelMatch(CSockMapping *cskmap_, u_long send_label_,
                     size_t num_bytes_)
        : cskmap(cskmap_), send_label(send_label_), num_bytes(num_bytes_) {}

    bool operator()(CSocketPtr csock) {
        // already locked the sockset_mutex here, since I'm inside find_csock
        // also, since this is only called from csock_with_labels,
        // I know that the multisocket is already locked.
        return cskmap->csock_matches_internal(get_pointer(csock), 
                                              send_label, num_bytes,
                                              true, true);
    }
};

CSocketPtr 
CSockMapping::csock_with_labels(u_long send_label)
{
    return csock_with_labels(send_label, 0);
}

CSocketPtr 
CSockMapping::csock_with_labels(u_long send_label, size_t num_bytes)
{
    // return a CSocketPtr that matches send_label
    return find_csock(GlobalLabelMatch(this, send_label, num_bytes));
}

struct IsNotBusy {
    bool operator()(CSocketPtr csock) {
        return !csock->is_busy();
    }
};

CSocketPtr
CSockMapping::get_idle_csock(bool grab_lock_first)
{
    return find_csock(IsNotBusy());
}


struct SatisfiesNetworkRestrictions {
    u_long send_labels;
    SatisfiesNetworkRestrictions(u_long send_labels_) : send_labels(send_labels_) {}
    bool operator()(CSocketPtr csock) {
        // add the trouble-check to match the behavior of
        //  {new|connected}_csock_with_labels.
        return csock->fits_net_restriction(send_labels);
        /* TODO-REDUNDANCY:   I should be sure to use redundancy for
         * TODO-REDUNDANCY:   FG IntNW control messages as well as FG data. */
    }
};

bool
CSockMapping::can_satisfy_network_restrictions(u_long send_labels)
{
    return bool(find_csock(SatisfiesNetworkRestrictions(send_labels)));
}

struct IfaceMatcher {
    struct net_interface local_iface;
    struct net_interface remote_iface;
    IfaceMatcher(struct net_interface li, 
                 struct net_interface ri) 
        : local_iface(li), remote_iface(ri) {}
    bool operator()(CSocketPtr csock) {
        return (csock->local_iface == local_iface &&
                csock->remote_iface == remote_iface);
    }
};

CSocketPtr
CSockMapping::csock_by_ifaces(struct net_interface local_iface,
                              struct net_interface remote_iface,
                              bool grab_lock)
{
    return find_csock(IfaceMatcher(local_iface, remote_iface), grab_lock);
}

CSocketPtr 
CSockMapping::connected_csock_with_labels(u_long send_label, bool locked)
{
    return connected_csock_with_labels(send_label, 0, locked);
}

CSocketPtr 
CSockMapping::connected_csock_with_labels(u_long send_label, 
                                          size_t num_bytes, bool locked)
{
    pair<struct net_interface, struct net_interface> iface_pair;

    CMMSocketImplPtr skp(sk);
    if (skp->isLoopbackOnly(locked)) { // grab the lock if not already holding it
        // only one socket ever, so just return it
        PthreadScopedRWLock lock(&sockset_mutex, false);
        if (available_csocks.empty()) {
            return CSocketPtr();
        } else {
            return *available_csocks.begin();
        }
    }

    map<pair<struct net_interface, struct net_interface>, CSocketPtr> lookup;

    PthreadScopedRWLock lock(&sockset_mutex, false);
    if (available_csocks.empty()) {
        return CSocketPtr();
    }

    GuardedNetworkChooser guarded_chooser = network_chooser->getGuardedChooser();
    guarded_chooser->reset();

    for (CSockSet::iterator it = available_csocks.begin();
         it != available_csocks.end(); it++) {
        CSocketPtr csock = *it;
        if (csock->is_connected()) {
            guarded_chooser->consider(csock);
            
            // keep track of the local/remote iface pair so I don't have to 
            //  iterate twice
            pair<struct net_interface, struct net_interface> key = 
                make_pair(csock->local_iface, csock->remote_iface);
            assert(lookup.count(key) == 0);
            lookup[key] = csock;
        }
    }

    if (lookup.empty()) {
        // no connected csocks
        return CSocketPtr();
    } else {
        if (guarded_chooser->choose_networks(send_label, num_bytes, 
                                             iface_pair.first, iface_pair.second)) {
            assert(lookup.count(iface_pair) > 0);
            return lookup[iface_pair];
        } else {
            return CSocketPtr();
        }
    }
}

/* must not be holding sk->scheduling_state_lock. */
bool CSockMapping::csock_matches(CSocket *csock, u_long send_label)
{
    return csock_matches_internal(csock, send_label, 0, false, false);
}

/* must not be holding sk->scheduling_state_lock. */
bool CSockMapping::csock_matches(CSocket *csock, u_long send_label, size_t num_bytes)
{
    return csock_matches_internal(csock, send_label, num_bytes, false, false);
}

bool CSockMapping::csock_matches_internal(CSocket *csock, u_long send_label, 
                                          size_t num_bytes,
                                          bool multisocket_already_locked, 
                                          bool sockset_already_locked)
{
    if (send_label == 0) {
        return true;
    }
    
    struct net_interface local_iface, remote_iface;
    bool got_ifaces = false;
    if (multisocket_already_locked) {
        got_ifaces = get_iface_pair_locked_internal(send_label, num_bytes, 
                                                    local_iface, remote_iface,
                                                    sockset_already_locked);
    } else {
        got_ifaces = get_iface_pair_internal(send_label, num_bytes, 
                                             local_iface, remote_iface,
                                             sockset_already_locked);
    }
    if (!got_ifaces) {
        // there is no interface pair that suits these labels;
        // therefore, csock must not be suitable!
        return false;
    }

    return (local_iface.ip_addr.s_addr == csock->local_iface.ip_addr.s_addr &&
            remote_iface.ip_addr.s_addr == csock->remote_iface.ip_addr.s_addr);
}

bool
CSockMapping::get_iface_pair(u_long send_label, size_t num_bytes,
                             struct net_interface& local_iface,
                             struct net_interface& remote_iface)
{
    return get_iface_pair_internal(send_label, num_bytes, 
                                   local_iface, remote_iface, 
                                   false);
}

bool
CSockMapping::get_iface_pair_internal(u_long send_label, size_t num_bytes,
                                      struct net_interface& local_iface,
                                      struct net_interface& remote_iface,
                                      bool sockset_already_locked)
{
    CMMSocketImplPtr skp(sk);
    PthreadScopedRWLock lock(&skp->my_lock, false); 

    // XXX: DEADLOCK between this and sockset_mutex.
    // bad ordering: usually, I hold skp->my_lock before sockset_mutex,
    // but when this comes from csock_with_labels
    //  by way of find_csock, I'm holding the sockset_mutex first.
    // HACKY-FIX: make sure that this is never called while holding sockset_mutex.
    // TODO: refactor.  classes should reach into each other less.
    
    return get_iface_pair_locked_internal(send_label, num_bytes,
                                          local_iface, remote_iface,
                                          sockset_already_locked);
}


bool
CSockMapping::get_iface_pair_locked(u_long send_label, size_t num_bytes,
                                    struct net_interface& local_iface,
                                    struct net_interface& remote_iface)
{
    return get_iface_pair_locked_internal(send_label, num_bytes,
                                          local_iface, remote_iface,
                                          false);
}

bool
CSockMapping::get_iface_pair_locked_internal(u_long send_label, 
                                             size_t num_bytes,
                                             struct net_interface& local_iface,
                                             struct net_interface& remote_iface,
                                             bool sockset_already_locked)
{
    CMMSocketImplPtr skp(sk);
    if (skp->isLoopbackOnly(false)) {
        // only ever use one interface pair, 
        //  so it's the one for all labels.
        local_iface = *skp->local_ifaces.begin();
        remote_iface = *skp->remote_ifaces.begin();
        return true;
    }

    GuardedNetworkChooser guarded_chooser = network_chooser->getGuardedChooser();
    guarded_chooser->reset();
    vector<CSocketPtr> csocks; // only one per iface pair
    for (NetInterfaceSet::iterator i = skp->local_ifaces.begin();
         i != skp->local_ifaces.end(); ++i) {
        for (NetInterfaceSet::iterator j = skp->remote_ifaces.begin();
             j != skp->remote_ifaces.end(); ++j) {
            csocks.clear();
            CSocketPtr existing_csock;
            if (!csocks.empty()) {
                existing_csock = csocks[0];
            }
            if (!existing_csock || count() == 1) {
                guarded_chooser->consider(*i, *j);
            } else {
                StringifyIP local_ip(&i->ip_addr);
                StringifyIP remote_ip(&j->ip_addr);
                dbgprintf("Not considering (%s -> %s) for labels %d; csock is troubled\n",
                          local_ip.c_str(), remote_ip.c_str(), (int) send_label);
            }
        }
    }

    return guarded_chooser->choose_networks(send_label, num_bytes,
                                            local_iface, remote_iface);
}

CSocketPtr
CSockMapping::make_new_csocket(struct net_interface local_iface, 
                               struct net_interface remote_iface,
                               int accepted_sock)
{
    CSocketPtr csock;
    bool should_data_check = false;
    {
        PthreadScopedRWLock lock(&sockset_mutex, true);
        
        csock = csock_by_ifaces(local_iface, remote_iface, false);
        if (csock)  {
            if (accepted_sock > 0) {
                // The accepting side thinks it still has a socket
                //   on this iface-pair, but the connecting side is
                //   connecting a new one.  That means that the connecting side
                //   thinks its connection is dead.
                // It seems like this shouldn't happen, but I get
                //   strange "Connection reset by peer" errors sometimes.
                // When it does happen, the accepting side should ditch its old
                //   csocket and use the new one instead.
                available_csocks.erase(csock);
                shutdown(csock->osfd, SHUT_RDWR); /* tells the sender/receiver threads to exit */
                should_data_check = true;
            } else {
                return csock;
            }
        }

        // TODO: if this multisocket was created by cmm_accept,
        //  it shouldn't be attempting new connections;
        //  let the cmm_connect()-ing side do that.
        
        csock = CSocket::create(sk, local_iface, remote_iface,
                                accepted_sock);
        /* cleanup if constructor throws */
        
        available_csocks.insert(csock);
        
        // XXX: this results in the same estimates being inserted twice 
        // XXX: right around the time that wifi fails.
        // XXX: why?  the socket fails first, and we try to create
        // XXX:  a new socket, not realizing that the network is gone
        // XXX:  (because we haven't received the scout update yet).
        // XXX: possible workaround: don't call this until the connection
        // XXX:  is established (hello received). Only one connection
        // XXX:  should succeed per wifi period.
        // XXX: On the other hand, what if we walk away from and then 
        // XXX:  back towards the same AP?  That would look the same
        // XXX:  (duplicated scout update), and it probably shouldn't
        // XXX:  trigger a zero-error insertion either.
        // XXX: Maybe I need to only insert new stats if the AP has changed.
        // XXX:  the scout doesn't currently send that info to the app,
        // XXX:  but it could.
        // Moved it to csocket.cpp, phys_connect.
        //csock->stats.getStats(network_chooser, csock->network_type());
    }

    csock->startup_workers(); // sender thread calls phys_connect()
    if (should_data_check) {
        CMMSocketImplPtr skp(sk);
        PthreadScopedLock lock(&skp->scheduling_state_lock);
        skp->data_check_all_irobs(local_iface.ip_addr.s_addr, 
                                  remote_iface.ip_addr.s_addr);
    }
    
    return csock;    
}

CSocketPtr 
CSockMapping::new_csock_with_labels(u_long send_label, bool grab_lock)
{
    return new_csock_with_labels(send_label, 0, grab_lock);
}

CSocketPtr 
CSockMapping::new_csock_with_labels(u_long send_label, size_t num_bytes, 
                                    bool grab_lock)
{
    {
        CSocketPtr csock = csock_with_labels(send_label, num_bytes);
        if (csock) {
            return csock;
        }
    }
    
    struct net_interface local_iface, remote_iface;
    bool result;
    if (grab_lock) {
        result = get_iface_pair(send_label, num_bytes, 
                                local_iface, remote_iface);
    } else {
        result = get_iface_pair_locked(send_label, num_bytes,
                                       local_iface, remote_iface);
    }
    if (!result) {
        /* Can't make a suitable connection for this send label */
        return CSocketPtr();
    }

    return make_new_csocket(local_iface, remote_iface);
}

int
CSockMapping::get_csock(PendingSenderIROB *psirob, CSocketPtr& csock) 
{
    u_long send_labels = 0;
    size_t num_bytes = 0;
    if (psirob) {
        send_labels = psirob->get_send_labels();
        if (psirob->is_complete()) {
            num_bytes = psirob->expected_bytes();
        }
    }

    struct net_interface local, remote;
    if (get_iface_pair_locked(send_labels, num_bytes, local, remote)) {
        // avoid using sockets that aren't yet connected; if connect() times out,
        //   it might take a long time to send anything
        csock = connected_csock_with_labels(send_labels, num_bytes, false);
        if (!csock) {
            csock = new_csock_with_labels(send_labels, num_bytes, false);
        }
        if (!csock) {
            csock = csock_by_ifaces(local, remote);
        }
    } else {
        csock.reset();
    }

    if (psirob) {
        check_redundancy(psirob);
    }
    
    if (!csock) {
        if (!can_satisfy_network_restrictions(send_labels)) {
            return CMM_UNDELIVERABLE;
        } else {
            return CMM_FAILED;
        }
    } else {
        return 0;
    }
}

void
CSockMapping::check_redundancy(PendingSenderIROB *psirob)
{
    irob_id_t id = psirob->get_id();
    dbgprintf("Checking whether to send IROB %ld redundantly...\n", id);
    if (count_connected_locked() <= 1) {
        dbgprintf("No extra interfaces; no redundancy\n");
        // no redundancy possible
        return;
    }
    
    u_long send_labels = psirob->get_send_labels();
    if (!(send_labels & CMM_LABEL_ONDEMAND &&
          send_labels & CMM_LABEL_SMALL)) {
        dbgprintf("IROB %ld is not (FG & SMALL); no redundancy\n", id);
        return;
    }

    if (network_chooser->shouldTransmitRedundantly(psirob)) {
        dbgprintf("Decided to send IROB %ld redundantly\n", id);
        psirob->mark_send_on_all_networks();
    } else {
        dbgprintf("Decided NOT to send IROB %ld redundantly\n", id);
    }
}

void 
CSockMapping::remove_csock(CSocketPtr victim)
{
    ASSERT(victim);
    PthreadScopedRWLock lock(&sockset_mutex, true);
    available_csocks.erase(victim);
    // CSockets are reference-counted by the 
    // CSocketSender and CSocketReceiver objects,
    // so we don't delete CSockets anywhere else

    victim->stats.mark_irob_failures(network_chooser, victim->network_type());
    network_chooser->reportNetworkTeardown(victim->network_type());
}

class AddrMatch {
  public:
    AddrMatch(struct in_addr addr_) : addr(addr_) {}
    bool operator()(struct net_interface iface) {
        return addr.s_addr == iface.ip_addr.s_addr;
    }
  private:
    struct in_addr addr;
};

bool
CSockMapping::get_iface_by_addr(const NetInterfaceSet& ifaces, 
                                struct in_addr addr,
                                struct net_interface& iface)
{
    CMMSocketImplPtr skp(sk);

    PthreadScopedRWLock lock(&skp->my_lock, false);

    NetInterfaceSet::const_iterator it = find_if(ifaces.begin(), 
                                                 ifaces.end(), 
                                                 AddrMatch(addr));
    if (it != ifaces.end()) {
        iface = *it;
        return true;
    } else {
        return false;
    }
}

bool
CSockMapping::get_local_iface_by_addr(struct in_addr addr,
                                      struct net_interface& iface)
{
    CMMSocketImplPtr skp(sk);
    return get_iface_by_addr(skp->local_ifaces, addr, iface);
}

bool
CSockMapping::get_remote_iface_by_addr(struct in_addr addr, 
                                      struct net_interface& iface)
{
    CMMSocketImplPtr skp(sk);
    return get_iface_by_addr(skp->remote_ifaces, addr, iface);
}

void
CSockMapping::add_connection(int sock, 
                             struct in_addr local_addr, 
                             struct net_interface remote_iface)
{
    dbgprintf("Adding new connection on %s from %s\n",
              StringifyIP(&local_addr).c_str(),
              StringifyIP(&remote_iface.ip_addr).c_str());
    struct net_interface local_iface;
    if (!get_local_iface_by_addr(local_addr, local_iface)) {
        /* XXX: not true! Our fake scout doesn't really simulate
         * the network disappearing.  Hosts can still 
         * try to connect to it, and they won't be refused. 
         * We'll get here, but the scout won't have told us about 
         * the new interface yet.  Maybe this should just add the
         * new local_iface. */

        /* should always know about my own interfaces
         * before anyone else does */
        //ASSERT(0);

        local_iface.ip_addr = local_addr;
        CMMSocket::interface_up(local_iface);
    }
    struct net_interface dummy;
    if (!get_remote_iface_by_addr(remote_iface.ip_addr, dummy)) {
        /* A remote interface that we didn't know about! */
        CMMSocketImplPtr skp(sk);
        skp->setup(remote_iface, false);
    }
    

    CSocketPtr csock = make_new_csocket(local_iface, remote_iface, sock);

    // the CSocketReceiver thread does this now.
    //csock->send_confirmation();
}

struct CSockMapping::get_worker_tids {
    vector<pthread_t>& workers;
    get_worker_tids(vector<pthread_t>& w) : workers(w) {}
    int operator()(CSocketPtr csock) {
        if (csock->csock_sendr) {
            workers.push_back(csock->csock_sendr->tid);
        }
        if (csock->csock_recvr) {
            workers.push_back(csock->csock_recvr->tid);
        }
        return 0;
    }
};

void
CSockMapping::join_to_all_workers()
{
    vector<pthread_t> workers;
    get_worker_tids getter(workers);
    (void)for_each(getter);

    void **ret = NULL;
    std::for_each(workers.begin(), workers.end(),
                  bind2nd(ptr_fun(pthread_join), ret));
}

struct CSockMapping::AddRequestIfNotSent {
    PendingSenderIROB *psirob;
    const IROBSchedulingData& data;

    AddRequestIfNotSent(PendingSenderIROB *psirob_, const IROBSchedulingData& data_)
        : psirob(psirob_), data(data_) {}
    
    int operator()(CSocketPtr csock) {
        if (!data.chunks_ready) {
            if (!psirob->was_announced(get_pointer(csock))) {
                csock->irob_indexes.new_irobs.insert(data);
            }
        } else {
            if (psirob->num_ready_bytes(get_pointer(csock)) > 0) {
                csock->irob_indexes.new_chunks.insert(data);
            }
        }
        return 0;
    }
};

void
CSockMapping::pass_request_to_all_senders(PendingSenderIROB *psirob,
                                          const IROBSchedulingData& data)
{
    if (data.data_check) {
        // don't make other threads send this redundantly;
        //  Data_check now only follows a loss of a network.
        // (It won't be used during redundancy re-evaluation either;
        //  that will just mark the IROB redundant and tell the senders
        //  about it again.)
        //sk->irob_indexes.waiting_data_checks.insert(data);
        return;
    } 

    AddRequestIfNotSent functor(psirob, data);
    for_each(functor);
}

void
CSockMapping::onRedundancyDecision(const IROBSchedulingData& data)
{
    CMMSocketImplPtr skp(sk);
    PthreadScopedLock lock(&skp->scheduling_state_lock);
    PendingIROBPtr pirob = skp->outgoing_irobs.find(data.id);
    if (pirob == NULL) {
        // must have been acknowledged
        return;
    }
    PendingSenderIROB *psirob = dynamic_cast<PendingSenderIROB*>(get_pointer(pirob));
    ASSERT(psirob);
    
    check_redundancy(psirob);
    if (psirob->should_send_on_all_networks()) {
        pass_request_to_all_senders(psirob, data);
        pthread_cond_broadcast(&skp->scheduling_state_cv);
    } else {
        if (Config::getInstance()->getPeriodicReevaluationEnabled() &&
            count_connected() > 1) {
            network_chooser->scheduleReevaluation(this, psirob, data);
        }
    }
}

void
CSockMapping::check_redundancy_async(PendingSenderIROB *psirob, 
                                     const IROBSchedulingData& data)
{
    network_chooser->checkRedundancyAsync(this, psirob, data);
}
