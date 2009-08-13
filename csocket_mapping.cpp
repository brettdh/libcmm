#include "csocket_mapping.h"
#include "csocket.h"
#include "signals.h"
#include "debug.h"
#include <memory>
#include <map>
#include <vector>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "cmm_socket.private.h"

using std::auto_ptr;
using std::pair;
using std::vector;

#include "tbb/spin_rw_mutex.h"

class LabelMatch {
  public:
    LabelMatch(u_long label_) : label(label_) {}
    virtual bool operator()(CSocket *csock) { return label == 0; };
    virtual bool operator()(CSocketPtr csock) { return label == 0; };
    virtual ~LabelMatch() {}
  protected:
    u_long label;
};

class LocalLabelMatch : public LabelMatch {
  public:
    LocalLabelMatch(u_long label_) : LabelMatch(label_) {}
    virtual bool operator()(CSocket *csock) {
        return (label == 0) || (csock->local_iface.labels & label);
    }
    virtual bool operator()(CSocketPtr csock) {
        return operator()(get_pointer(csock));
    }
};

class RemoteLabelMatch : public LabelMatch {
  public:
    RemoteLabelMatch(u_long label_) : LabelMatch(label_) {}
    virtual bool operator()(CSocket *csock) {
        return (label == 0) || (csock->remote_iface.labels & label);
    }
    virtual bool operator()(CSocketPtr csock) {
        return operator()(get_pointer(csock));
    }
};

class BothLabelsMatch : public LabelMatch {
  public:
    BothLabelsMatch(u_long send_label, u_long recv_label)
        : LabelMatch(0), /* ignored */
          local_match(send_label), remote_match(recv_label) {}
    virtual bool operator()(CSocket *csock) {
        return local_match(csock) && remote_match(csock);
    }
    virtual bool operator()(CSocketPtr csock) {
        return operator()(get_pointer(csock));
    }
  private:
    LocalLabelMatch local_match;
    RemoteLabelMatch remote_match;
};
    
CSockMapping::CSockMapping(CMMSocketImpl *sk_)
    : sk(sk_)
{
    /* empty */
}

CSockMapping::~CSockMapping()
{
#if 0
    scoped_rwlock lock(sockset_mutex, true);
    CSockSet victims = connected_csocks;
    connected_csocks.clear();
    lock.release();
    
    for (CSockSet::iterator it = victims.begin();
	 it != victims.end(); it++) {
	CSocket *victim = *it;
	delete victim;
    }
#endif
}

bool
CSockMapping::empty()
{
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
    (void)for_each(push_osfd(sk->sock, osfd_list));
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

    scoped_rwlock lock(sockset_mutex, true);
    while (!victims.empty()) {
        CSocketPtr victim = victims.back();
        victims.pop_back();
        connected_csocks.erase(victim);
        //delete victim; /* closes socket, cleans up */
    }
}

CSocketPtr 
CSockMapping::csock_with_send_label(u_long label)
{
    return find_csock(LocalLabelMatch(label));
}

CSocketPtr 
CSockMapping::csock_with_recv_label(u_long label)
{
    return find_csock(RemoteLabelMatch(label));
}

CSocketPtr 
CSockMapping::csock_with_labels(u_long send_label, u_long recv_label)
{
    return find_csock(BothLabelsMatch(send_label, recv_label));
}

bool
CSockMapping::csock_matches(CSocket *csock, 
                            u_long send_label, u_long recv_label)
{
    return BothLabelsMatch(send_label, recv_label)(csock);
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
    return get_iface(sk->local_ifaces, label, iface);
}

bool
CSockMapping::get_remote_iface(u_long label, struct net_interface& iface)
{
    return get_iface(sk->remote_ifaces, label, iface);
}

CSocketPtr 
CSockMapping::new_csock_with_labels(u_long send_label, u_long recv_label)
{
    {
        CSocketPtr csock = csock_with_labels(send_label, recv_label);
        if (csock) {
            return csock;
        }
    }

    struct net_interface local_iface, remote_iface;
    if (!(get_local_iface(send_label, local_iface) &&
          get_remote_iface(recv_label, remote_iface))) {
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
	scoped_rwlock lock(sockset_mutex, true);
	connected_csocks.insert(csock);
    }
    // to interrupt any select() in progress, adding the new osfd
    dbgprintf("Interrupting any selects() in progress to add osfd %d "
	      "to multi-socket %d\n",
	      csock->osfd, sk->sock);
    signal_selecting_threads();
    
    return csock;
}

void 
CSockMapping::remove_csock(CSocketPtr victim)
{
    assert(victim);
    scoped_rwlock lock(sockset_mutex, true);
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
    return get_iface_by_addr(sk->local_ifaces, addr, iface);
}

bool
CSockMapping::get_remote_iface_by_addr(struct in_addr addr, 
                                      struct net_interface& iface)
{
    return get_iface_by_addr(sk->remote_ifaces, addr, iface);
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

    {
	scoped_rwlock lock(sockset_mutex, true);
	connected_csocks.insert(new_csock);
    }
    signal_selecting_threads();
}
