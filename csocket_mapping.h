#ifndef csocket_mapping_h_incl
#define csocket_mapping_h_incl

#include <set>
#include <map>
#include <netinet/in.h>
#include <sys/types.h>
#include "common.h"
#include "net_interface.h"
#include "pthread_util.h"

#include "csocket.h"

typedef std::set<CSocketPtr> CSockSet;

class LabelMatch;

typedef std::map<u_long, std::map<u_long, CSocketPtr> > CSockLabelMap;

class CMMSocketImpl;

class CSockMapping {
  public:
    // return true iff csock is suitable for these labels.
    /* must not be holding sk->scheduling_state_lock. */
    bool csock_matches(CSocket *csock, 
                       u_long send_label);

    CSocketPtr csock_with_labels(u_long send_label);
    CSocketPtr connected_csock_with_labels(u_long send_label, bool locked=true);
    CSocketPtr new_csock_with_labels(u_long send_label, bool locked=true);
    void remove_csock(CSocketPtr csock); // only removes, doesn't delete

    bool get_local_iface_by_addr(struct in_addr addr, 
                                 struct net_interface& iface);
    bool get_iface_pair(u_long send_label,
                        struct net_interface& local_iface,
                        struct net_interface& remote_iface,
                        bool locked=true);


    bool empty();
    
    void add_connection(int sock, 
                        struct in_addr local_addr,
                        struct net_interface remote_iface);

    /* append <mc_socket_t,osfd> pairs to this vector for each 
     * such mapping in this mc_socket. */
    void get_real_fds(mcSocketOsfdPairList &osfd_list);

    void setup(struct net_interface iface, bool local);
    void teardown(struct net_interface iface, bool local);

    // only call when shutting down.
    // waits (pthread_join) for all workers to exit.
    void join_to_all_workers();

    CSockMapping(CMMSocketImplPtr sk);
    ~CSockMapping();

    template <typename Functor>
    void for_each_by_ref(Functor& f);

    /* Functor must define int operator()(CSocketPtr), 
     * a function that returns -1 on error or >=0 on success */
    template <typename Functor>
    int for_each(Functor f);
  private:
    //CSockLabelMap csocks_by_send_label;
    boost::weak_ptr<CMMSocketImpl> sk;  /* XXX: janky.  Remove later? */
    CSockSet available_csocks;
    pthread_rwlock_t sockset_mutex;

    struct get_worker_tids;

    bool get_iface(const NetInterfaceSet& ifaces, u_long label,
                   struct net_interface& iface, bool locked);
    bool get_remote_iface_by_addr(struct in_addr addr,
                                  struct net_interface& iface);
    bool get_iface_by_addr(const NetInterfaceSet& ifaces, struct in_addr addr,
                           struct net_interface& iface);

    template <typename Predicate>
    CSocketPtr find_csock(Predicate pred);
};

template <typename Functor>
void CSockMapping::for_each_by_ref(Functor& f)
{
    PthreadScopedRWLock lock(&sockset_mutex, false);
    for (CSockSet::iterator it = available_csocks.begin();
	 it != available_csocks.end(); it++) {
	CSocketPtr csock = *it;
	f(csock);
    }
}

template <typename Functor>
int CSockMapping::for_each(Functor f)
{
    PthreadScopedRWLock lock(&sockset_mutex, false);
    for (CSockSet::iterator it = available_csocks.begin();
	 it != available_csocks.end(); it++) {
	CSocketPtr csock = *it;
	int rc = f(csock);
	if (rc < 0) {
	    return rc;
	}
    }
    return 0;
}

template <typename Predicate>
CSocketPtr 
CSockMapping::find_csock(Predicate pred)
{
    PthreadScopedRWLock lock(&sockset_mutex, false);
    CSockSet::const_iterator it = find_if(available_csocks.begin(), 
					  available_csocks.end(), 
					  pred);
    if (it == available_csocks.end()) {
        return CSocketPtr();
    } else {
	return *it;
    }
}


#endif
