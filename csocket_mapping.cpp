#include "csocket_mapping.h"
#include "csocket.h"
#include "csocket_sender.h"
#include "csocket_receiver.h"
#include "signals.h"
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

using std::auto_ptr;
using std::pair;
using std::vector;

#include "pthread_util.h"
    
CSockMapping::CSockMapping(CMMSocketImplPtr sk_)
    : sk(sk_)
{
    pthread_rwlock_init(&sockset_mutex, NULL);
}

CSockMapping::~CSockMapping()
{
    PthreadScopedRWLock lock(&sockset_mutex, true);
    connected_csocks.clear();
}

bool
CSockMapping::empty()
{
    PthreadScopedRWLock lock(&sockset_mutex, true);
    return connected_csocks.empty();
}

struct push_osfd {
    mc_socket_t mc_sock;
    mcSocketOsfdPairList &osfd_list;
    push_osfd(mc_socket_t mcs, mcSocketOsfdPairList& list) 
	: mc_sock(mcs), osfd_list(list) {}
    int operator()(CSocketPtr csock) {
	assert(csock);
        int osfd = csock->osfd;
        assert(osfd > 0);

        osfd_list.push_back(pair<mc_socket_t,int>(mc_sock,osfd));
	return 0;
    }
};

void
CSockMapping::get_real_fds(mcSocketOsfdPairList &osfd_list)
{
    (void)for_each(push_osfd(CMMSocketImplPtr(sk)->sock, osfd_list));
}

struct get_matching_csocks {
    const struct net_interface& iface;
    vector<CSocketPtr>& matches;
    bool local;
    get_matching_csocks(const struct net_interface& iface_,
                        vector<CSocketPtr>& matches_, bool local_)
	: iface(iface_), matches(matches_), local(local_) {}

    int operator()(CSocketPtr csock) {
        assert(csock);

        struct net_interface *candidate = NULL;
        if (local) {
            candidate = &csock->local_iface;
        } else {
            candidate = &csock->remote_iface;
        }
        if (candidate->ip_addr.s_addr == iface.ip_addr.s_addr) {
            matches.push_back(csock);
        }
	return 0;
    }
};

/* already holding sk->my_lock, writer=true */
void 
CSockMapping::setup(struct net_interface iface, bool local)
{
    vector<CSocketPtr> matches;
    (void)for_each(get_matching_csocks(iface, matches, local));

    for (size_t i = 0; i < matches.size(); ++i) {
        // replace connection stats with updated numbers
        if (local) {
            matches[i]->local_iface = iface;
        } else {
            matches[i]->remote_iface = iface;
        }
    }
}

void
CSockMapping::teardown(struct net_interface iface, bool local)
{
    vector<CSocketPtr> victims;
    (void)for_each(get_matching_csocks(iface, victims, local));

    PthreadScopedRWLock lock(&sockset_mutex, true);
    while (!victims.empty()) {
        CSocketPtr victim = victims.back();
        victims.pop_back();
        connected_csocks.erase(victim);
        shutdown(victim->osfd, SHUT_RDWR); /* tells the sender/receiver threads to exit */
    }
}

struct LabelMatch {
    CSockMapping *cskmap;
    u_long send_label;

    LabelMatch(CSockMapping *cskmap_, u_long send_label_)
        : cskmap(cskmap_), send_label(send_label_) {}

    bool operator()(CSocketPtr csock) {
        return cskmap->csock_matches(get_pointer(csock), send_label);
    }
};

CSocketPtr 
CSockMapping::csock_with_labels(u_long send_label)
{
    // return a CSocketPtr that matches send_label
    return find_csock(LabelMatch(this, send_label));
}

/* must not be holding sk->scheduling_state_lock. */
bool
CSockMapping::csock_matches(CSocket *csock, u_long send_label)
{
    if (send_label == 0) {
        return true;
    }

    struct net_interface local_iface, remote_iface;
    if (!get_iface_pair(send_label, local_iface, remote_iface, true)) {
        // there is no interface pair that suits these labels, 
        // so therefore csock must not be suitable!
        return false;
    }

    return (local_iface.ip_addr.s_addr == csock->local_iface.ip_addr.s_addr &&
            remote_iface.ip_addr.s_addr == csock->remote_iface.ip_addr.s_addr);
}

/*
class IfaceMatch {
  public:
    IfaceMatch(u_long label_) : label(label_) {}
    bool operator()(struct net_interface iface) {
        return ((label == 0) 
                || (iface.labels == 0)
                || (iface.labels & label));;
    }
  private:
    u_long label;
};

bool
CSockMapping::get_iface(const NetInterfaceSet& ifaces, u_long label,
                        struct net_interface& iface, bool locked)
{
    CMMSocketImplPtr skp(sk);

    // same scope effect, but only if locked==true
    auto_ptr<PthreadScopedRWLock> lock_ptr;
    if (locked) {
        lock_ptr.reset(new PthreadScopedRWLock(&skp->my_lock, false));
    }

    NetInterfaceSet::const_iterator it = find_if(ifaces.begin(), 
                                                 ifaces.end(), 
                                                 IfaceMatch(label));
    if (it != ifaces.end()) {
        iface = *it;
        return true;
    } else {
        return false;
    }
}
*/

bool
CSockMapping::get_iface_pair(u_long send_label,
                             struct net_interface& local_iface,
                             struct net_interface& remote_iface, 
                             bool locked)
{
    CMMSocketImplPtr skp(sk);
    auto_ptr<PthreadScopedRWLock> lock_ptr;
    if (locked) {
        lock_ptr.reset(new PthreadScopedRWLock(&skp->my_lock, false));
    }

    // these numbers are arbitrary.
    // they serve to prevent a low-bandwidth network
    // from being used for large BG traffic (unless
    // it's trickling)
    const u_long MIN_BG_BW = 25000;
    //const u_long MAX_FG_RTT = 50;

    u_long max_bw = 0;
    u_long min_RTT = ULONG_MAX;
    pair<struct net_interface, struct net_interface> max_bw_iface_pair;
    pair<struct net_interface, struct net_interface> min_RTT_iface_pair;
    
    for (NetInterfaceSet::iterator i = skp->local_ifaces.begin();
         i != skp->local_ifaces.end(); ++i) {
        for (NetInterfaceSet::iterator j = skp->remote_ifaces.begin();
             j != skp->remote_ifaces.end(); ++j) {
            //u_long bw = iface_bandwidth(*i, *j);
            //u_long RTT = iface_RTT(*i, *j);
            u_long bw, RTT;
            if (!NetStats::get_estimate(*i, *j, NET_STATS_BW_UP, bw)) {
                bw = iface_bandwidth(*i, *j);
            }
            if (!NetStats::get_estimate(*i, *j, NET_STATS_LATENCY, RTT)) {
                RTT = iface_RTT(*i, *j);
            } else {
                RTT = RTT * 2;
            }
            
            if (bw > max_bw) {
                max_bw = bw;
                max_bw_iface_pair = make_pair(*i, *j);
            }
            if (RTT < min_RTT) {
                min_RTT = RTT;
                min_RTT_iface_pair = make_pair(*i, *j);
            }
        }
    }
    
    if (send_label & CMM_LABEL_ONDEMAND) {
        if (send_label & CMM_LABEL_SMALL &&
            min_RTT < ULONG_MAX) {
            local_iface = min_RTT_iface_pair.first;
            remote_iface = min_RTT_iface_pair.second;
            return true;
        } else if (send_label & CMM_LABEL_LARGE) {
            local_iface = max_bw_iface_pair.first;
            remote_iface = max_bw_iface_pair.second;
            return true;
        } else {
            // TODO: try to check based on the actual size
            local_iface = min_RTT_iface_pair.first;
            remote_iface = min_RTT_iface_pair.second;
            return true;            
        }
    } else if (send_label & CMM_LABEL_BACKGROUND &&
               max_bw > MIN_BG_BW) {
        local_iface = max_bw_iface_pair.first;
        remote_iface = max_bw_iface_pair.second;
        return true;
    } else if (send_label == 0) {
        local_iface = *(skp->local_ifaces.begin());
        remote_iface = *(skp->remote_ifaces.begin());
        return true;
    }
    
    return false;
}

CSocketPtr 
CSockMapping::new_csock_with_labels(u_long send_label, bool locked)
{
    {
        CSocketPtr csock = csock_with_labels(send_label);
        if (csock) {
            return csock;
        }
    }
    
    struct net_interface local_iface, remote_iface;
    if (!get_iface_pair(send_label, local_iface, remote_iface, locked)) {
        /* Can't make a suitable connection for this send label */
        return CSocketPtr();
    }

    CSocketPtr csock(CSocket::create(sk, local_iface, remote_iface));
    /* cleanup if constructor throws */

    int rc = csock->phys_connect();
    if (rc < 0) {
	if (errno==EINPROGRESS || errno==EWOULDBLOCK) {
	    /* XXX: handle this sanely for non-blocking connect. */
	    //is this what we want for the 'send', 
	    //i.e wait until the sock is conn'ed.
	    errno = EAGAIN;
	} else {
	    dbgprintf("Failed to connect new csock\n");
	    //delete csock;
	    return CSocketPtr();
	}
    }

    {
	PthreadScopedRWLock lock(&sockset_mutex, true);
	connected_csocks.insert(csock);
    }
    // to interrupt any select() in progress, adding the new osfd
    /*
    dbgprintf("Interrupting any selects() in progress to add osfd %d "
	      "to multi-socket %d\n",
	      csock->osfd, CMMSocketImplPtr(sk)->sock);
    signal_selecting_threads();
    */
    
    return csock;
}

void 
CSockMapping::remove_csock(CSocketPtr victim)
{
    assert(victim);
    PthreadScopedRWLock lock(&sockset_mutex, true);
    connected_csocks.erase(victim);
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
        //assert(0);

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
    
    
    CSocketPtr new_csock(CSocket::create(sk, local_iface, remote_iface, 
                                         sock));
    
    new_csock->startup_workers();
    {
	PthreadScopedRWLock lock(&sockset_mutex, true);
	connected_csocks.insert(new_csock);
    }
    //signal_selecting_threads();
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
    (void)for_each(get_worker_tids(workers));

    void **ret = NULL;
    std::for_each(workers.begin(), workers.end(),
                  bind2nd(ptr_fun(pthread_join), ret));
}
