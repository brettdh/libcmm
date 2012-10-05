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
#include "irob_scheduling.h"

using std::auto_ptr;
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
    redundancy_strategy = RedundancyStrategy::create(NEVER_REDUNDANT);
}

CSockMapping::~CSockMapping()
{
    PthreadScopedRWLock lock(&sockset_mutex, true);
    available_csocks.clear();
    
    delete redundancy_strategy;
}

void
CSockMapping::set_redundancy_strategy(int type)
{
    assert(redundancy_strategy);
    delete redundancy_strategy;
    redundancy_strategy = RedundancyStrategy::create(type);
}

int 
CSockMapping::get_redundancy_strategy()
{
    assert(redundancy_strategy);
    return redundancy_strategy->getType();
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
                                 matches[i]->remote_iface);
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
                (void)make_new_csocket(local_iface, remote_iface);
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
        shutdown(victim->osfd, SHUT_RDWR); /* tells the sender/receiver threads to exit */
    }
}

// function operator returns true iff this CSocket is the
//  best match for the labels, given all the available interfaces.
struct GlobalLabelMatch {
    CSockMapping *cskmap;
    u_long send_label;

    GlobalLabelMatch(CSockMapping *cskmap_, u_long send_label_)
        : cskmap(cskmap_), send_label(send_label_) {}

    bool operator()(CSocketPtr csock) {
        // already locked the sockset_mutex here, since I'm inside find_csock
        return cskmap->csock_matches_internal(get_pointer(csock), send_label, false, true);
    }
};

CSocketPtr 
CSockMapping::csock_with_labels(u_long send_label)
{
    // return a CSocketPtr that matches send_label
    return find_csock(GlobalLabelMatch(this, send_label));
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
        return (csock->fits_net_restriction(send_labels)
                /*
                && (send_labels & CMM_LABEL_BACKGROUND || 
                !csock->is_in_trouble())
                */
                );
        /* TODO-REDUNDANCY: the trouble-check will go away,
         * TODO-REDUNDANCY:   but I should be sure to use redundancy for
         * TODO-REDUNDANCY:   FG IntNW control messages as well as FG data. */
    }
};

bool
CSockMapping::can_satisfy_network_restrictions(u_long send_labels)
{
    return find_csock(SatisfiesNetworkRestrictions(send_labels));
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

// Call consider() with several different label pairs, then
//  call pick_label_match to get the local and remote
//  interfaces among the pairs considered that best match
//  the labels.
struct LabelMatcher {
    u_long max_bw;
    u_long min_RTT;
    bool has_match;
    bool has_wifi_match;
    bool has_threeg_match;
    pair<struct net_interface, struct net_interface> max_bw_iface_pair;
    pair<struct net_interface, struct net_interface> min_RTT_iface_pair;

    pair<struct net_interface, struct net_interface> wifi_pair;
    pair<struct net_interface, struct net_interface> threeg_pair;

    LabelMatcher() : max_bw(0), min_RTT(ULONG_MAX), 
                     has_match(false), has_wifi_match(false), has_threeg_match(false) {}

    // for use with CSockMapping::for_each
    void consider(CSocketPtr csock) {
        consider(csock->local_iface, csock->remote_iface);
    }

    // Call this on each pair to be considered
    void consider(struct net_interface local_iface, 
                  struct net_interface remote_iface) {
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
    
    bool pick_label_match(u_long send_label,
                          struct net_interface& local_iface,
                          struct net_interface& remote_iface) {
        if (!has_match) {
            return false;
        }

        // first, check net type restriction labels, since they take precedence
        if (send_label & CMM_LABEL_WIFI_ONLY) {
            if (!has_wifi_match) {
                return false;
            }

            local_iface = wifi_pair.first;
            remote_iface = wifi_pair.second;
            return true;
        } else if (send_label & CMM_LABEL_THREEG_ONLY) {
            if (!has_threeg_match) {
                return false;
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
};

CSocketPtr 
CSockMapping::connected_csock_with_labels(u_long send_label, bool locked)
{
    LabelMatcher matcher;
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

    for (CSockSet::iterator it = available_csocks.begin();
         it != available_csocks.end(); it++) {
        CSocketPtr csock = *it;
        if (csock->is_connected()) {
            matcher.consider(csock);
            
            // keep track of the local/remote iface pair so I don't have to 
            //  iterate twice
            lookup[make_pair(csock->local_iface, csock->remote_iface)] = csock;
        }
    }

    if (lookup.empty()) {
        // no connected csocks
        return CSocketPtr();
    } else {
        if (matcher.pick_label_match(send_label, iface_pair.first, iface_pair.second)) {
            return lookup[iface_pair];
        } else {
            return CSocketPtr();
        }
    }
}

/* must not be holding sk->scheduling_state_lock. */
bool
CSockMapping::csock_matches_ignore_trouble(CSocket *csock, u_long send_label)
{
    // TODO-REDUNDANCY: this won't be necessary
    return csock_matches(csock, send_label, true);
}

/* must not be holding sk->scheduling_state_lock. */
bool CSockMapping::csock_matches(CSocket *csock, u_long send_label, bool ignore_trouble)
{
    // TODO-REDUNDANCY: ignore_trouble won't be necessary; it will always ignore trouble
    return csock_matches_internal(csock, send_label, ignore_trouble, false);
}

// TODO-REDUNDANCY: ignore_trouble won't be necessary; it will always ignore trouble
bool CSockMapping::csock_matches_internal(CSocket *csock, u_long send_label, bool ignore_trouble, 
                                          bool sockset_already_locked)
{
    if (send_label == 0) {
        return true;
    }
    
    struct net_interface local_iface, remote_iface;
    if (!get_iface_pair_internal(send_label, local_iface, remote_iface,
                                 ignore_trouble, sockset_already_locked)) {
        // there is no interface pair that suits these labels;
        // therefore, csock must not be suitable!
        return false;
    }

    return (local_iface.ip_addr.s_addr == csock->local_iface.ip_addr.s_addr &&
            remote_iface.ip_addr.s_addr == csock->remote_iface.ip_addr.s_addr);
}

// TODO-REDUNDANCY: ignore_trouble won't be necessary; it will always ignore trouble
bool
CSockMapping::get_iface_pair(u_long send_label,
                             struct net_interface& local_iface,
                             struct net_interface& remote_iface,
                             bool ignore_trouble)
{
    return get_iface_pair_internal(send_label, local_iface, remote_iface, 
                                   ignore_trouble, false);
}

// TODO-REDUNDANCY: ignore_trouble won't be necessary; it will always ignore trouble
bool
CSockMapping::get_iface_pair_internal(u_long send_label, 
                                      struct net_interface& local_iface,
                                      struct net_interface& remote_iface,
                                      bool ignore_trouble,
                                      bool sockset_already_locked)
{
    CMMSocketImplPtr skp(sk);
    PthreadScopedRWLock lock(&skp->my_lock, false);
    return get_iface_pair_locked_internal(send_label, local_iface, remote_iface,
                                          ignore_trouble, sockset_already_locked);
}

// TODO-REDUNDANCY: ignore_trouble won't be necessary; it will always ignore trouble
bool
CSockMapping::get_iface_pair_locked(u_long send_label,
                                    struct net_interface& local_iface,
                                    struct net_interface& remote_iface,
                                    bool ignore_trouble)
{
    return get_iface_pair_locked_internal(send_label, local_iface, remote_iface,
                                          ignore_trouble, false);
}

// TODO-REDUNDANCY: ignore_trouble won't be necessary; it will always ignore trouble
bool
CSockMapping::get_iface_pair_locked_internal(u_long send_label,
                                             struct net_interface& local_iface,
                                             struct net_interface& remote_iface,
                                             bool ignore_trouble,
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
    
    LabelMatcher matcher;
    vector<CSocketPtr> csocks; // only one per iface pair
    for (NetInterfaceSet::iterator i = skp->local_ifaces.begin();
         i != skp->local_ifaces.end(); ++i) {
        for (NetInterfaceSet::iterator j = skp->remote_ifaces.begin();
             j != skp->remote_ifaces.end(); ++j) {
            csocks.clear();
            /*
            if (!ignore_trouble) {
                get_matching_csocks getter(&*i, &*j, csocks);
                if (sockset_already_locked) {
                    for_each_locked(getter);
                } else {
                    for_each(getter);
                }
            }
            */
            CSocketPtr existing_csock;
            if (!csocks.empty()) {
                existing_csock = csocks[0];
            }
            if (!existing_csock ||

                // TODO-REDUNDANCY: this check goes away, because I want to consider
                // TODO-REDUNDANCY: all csockets. I should only send redundantly once.
                /*
                !existing_csock->is_in_trouble() ||
                send_label & CMM_LABEL_BACKGROUND || // ignore trouble for BG data
                */

                count() == 1) {
                matcher.consider(*i, *j);
            } else {
                char local_ip[16], remote_ip[16];
                get_ip_string(i->ip_addr, local_ip);
                get_ip_string(j->ip_addr, remote_ip);
                dbgprintf("Not considering (%s -> %s) for labels %d; csock is troubled\n",
                          local_ip, remote_ip, (int) send_label);
            }
        }
    }

    return matcher.pick_label_match(send_label, local_iface, remote_iface);
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
    {
        CSocketPtr csock = csock_with_labels(send_label);
        if (csock) {
            return csock;
        }
    }
    
    struct net_interface local_iface, remote_iface;
    bool result;
    if (grab_lock) {
        result = get_iface_pair(send_label, local_iface, remote_iface);
    } else {
        result = get_iface_pair_locked(send_label, local_iface, remote_iface);
    }
    if (!result) {
        /* Can't make a suitable connection for this send label */
        return CSocketPtr();
    }

    return make_new_csocket(local_iface, remote_iface);
}

int
CSockMapping::get_csock(PendingSenderIROB *psirob, CSocket*& csock)
{
    u_long send_labels = 0;
    if (psirob) {
        send_labels = psirob->get_send_labels();
    }

    struct net_interface local, remote;
    // ignore trouble-check when picking a socket here;
    //  if it's troubled, it'll hand off its data to another socket
    //  (or drop it)
    // TODO-REDUNDANCY: swap trouble check for should-be-redundant check
    if (get_iface_pair_locked(send_labels, local, remote, true)) {
        // avoid using sockets that aren't yet connected; if connect() times out,
        //   it might take a long time to send anything
        csock = get_pointer(connected_csock_with_labels(send_labels, false));
        if (!csock) {
            csock = get_pointer(new_csock_with_labels(send_labels, false));
        }
        if (!csock) {
            csock = get_pointer(csock_by_ifaces(local, remote));
        }
    } else {
        csock = NULL;
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
    if (count_locked() <= 1) {
        // no redundancy possible
        return;
    }
    
    u_long send_labels = psirob->get_send_labels();
    if (!(send_labels & CMM_LABEL_ONDEMAND &&
          send_labels & CMM_LABEL_SMALL)) {
        return;
    }

    if (redundancy_strategy->shouldTransmitRedundantly(psirob)) {
        psirob->mark_send_on_all_networks();
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
    dbgprintf("Adding new connection on %s ",
              inet_ntoa(local_addr));
    dbgprintf_plain("from %s\n", inet_ntoa(remote_iface.ip_addr));
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
        local_iface.labels = 0; /* will get updated by the scout */
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
    
    bool operator()(CSocketPtr csock) {
        CSocketSender *sender = csock->csock_sendr;
        if (!data.chunks_ready) {
            if (!psirob->was_announced(get_pointer(csock))) {
                csock->irob_indexes.new_irobs.insert(data);
            }
        } else {
            if (psirob->num_ready_bytes(get_pointer(csock)) > 0) {
                csock->irob_indexes.new_chunks.insert(data);
            }
        }
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
