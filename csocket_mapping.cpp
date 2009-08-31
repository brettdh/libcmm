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

#include "cmm_socket.private.h"

using std::auto_ptr;
using std::pair;
using std::vector;

#include "pthread_util.h"

class LabelMatch {
  public:
    LabelMatch(u_long label_) : label(label_) {}
    virtual bool operator()(CSocket *csock) { return label == 0; };
    virtual bool operator()(CSocketPtr csock) { return label == 0; };
    virtual ~LabelMatch() {}
  protected:
    u_long label;
};
    
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

struct get_victim_csocks {
    const struct net_interface& iface;
    vector<CSocketPtr>& victims;
    bool local;
    get_victim_csocks(const struct net_interface& iface_,
		   vector<CSocketPtr>& victims_, bool local_)
	: iface(iface_), victims(victims_), local(local_) {}

    int operator()(CSocketPtr csock) {
        assert(csock);

        struct net_interface *candidate = NULL;
        if (local) {
            candidate = &csock->local_iface;
        } else {
            candidate = &csock->remote_iface;
        }
        if (candidate->ip_addr.s_addr == iface.ip_addr.s_addr) {
            victims.push_back(csock);
        }
	return 0;
    }
};

void
CSockMapping::teardown(struct net_interface iface, bool local)
{
    vector<CSocketPtr> victims;
    (void)for_each(get_victim_csocks(iface, victims, local));

    PthreadScopedRWLock lock(&sockset_mutex, true);
    while (!victims.empty()) {
        CSocketPtr victim = victims.back();
        victims.pop_back();
        connected_csocks.erase(victim);
        //delete victim; /* closes socket, cleans up */
    }
}

CSocketPtr 
CSockMapping::csock_with_labels(u_long send_label)
{
    return find_csock(LabelMatch(send_label));
}

bool
CSockMapping::csock_matches(CSocket *csock, 
                            u_long send_label)
{
    return LabelMatch(send_label)(csock);
}

class IfaceMatch {
  public:
    IfaceMatch(u_long label_) : label(label_) {}
    bool operator()(struct net_interface iface) {
        return (label == 0) || (iface.labels & label);
    }
  private:
    u_long label;
};

bool
CSockMapping::get_iface(const NetInterfaceSet& ifaces, u_long label,
                        struct net_interface& iface)
{
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

bool
CSockMapping::get_local_iface(u_long label, struct net_interface& iface)
{
    CMMSocketImplPtr skp(sk);

    PthreadScopedLock lock(&skp->hashmaps_mutex);
    return get_iface(skp->local_ifaces, label, iface);
}

bool
CSockMapping::get_remote_iface(u_long label, struct net_interface& iface)
{
    CMMSocketImplPtr skp(sk);

    PthreadScopedLock lock(&skp->hashmaps_mutex);
    return get_iface(skp->remote_ifaces, label, iface);
}

CSocketPtr 
CSockMapping::new_csock_with_labels(u_long send_label)
{
    {
        CSocketPtr csock = csock_with_labels(send_label);
        if (csock) {
            return csock;
        }
    }

    struct net_interface local_iface, remote_iface;
    if (!(get_local_iface(send_label, local_iface) &&
          get_remote_iface(0, remote_iface))) { // pick any remote iface
        /* one of the desired labels wasn't available */
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
    dbgprintf("Interrupting any selects() in progress to add osfd %d "
	      "to multi-socket %d\n",
	      csock->osfd, CMMSocketImplPtr(sk)->sock);
    signal_selecting_threads();
    
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

    PthreadScopedLock lock(&skp->hashmaps_mutex);
    return get_iface_by_addr(skp->local_ifaces, addr, iface);
}

bool
CSockMapping::get_remote_iface_by_addr(struct in_addr addr, 
                                      struct net_interface& iface)
{
    CMMSocketImplPtr skp(sk);

    PthreadScopedLock lock(&skp->hashmaps_mutex);
    return get_iface_by_addr(skp->remote_ifaces, addr, iface);
}

void
CSockMapping::add_connection(int sock, 
                             struct in_addr local_addr, 
                             struct in_addr remote_addr)
{
    dbgprintf("Adding new connection on %s from %s\n",
	      inet_ntoa(local_addr), inet_ntoa(remote_addr));
    struct net_interface local_iface, remote_iface;
    if (!get_local_iface_by_addr(local_addr, local_iface) ||
        !get_remote_iface_by_addr(remote_addr, remote_iface)) {
        assert(0); /* XXX: valid? only creating a CSocket here
                    * if a connection has arrived on this iface pair */
    }
    
    
    CSocketPtr new_csock(CSocket::create(sk, local_iface, remote_iface, 
                                         sock));
    
    new_csock->startup_workers();
    {
	PthreadScopedRWLock lock(&sockset_mutex, true);
	connected_csocks.insert(new_csock);
    }
    signal_selecting_threads();
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
