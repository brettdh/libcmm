#ifndef csocket_mapping_h_incl
#define csocket_mapping_h_incl

#include <set>
#include <map>
#include <netinet/in.h>
#include <sys/types.h>
#include "common.h"
#include "net_interface.h"
#include "pthread_util.h"

#include "cmm_socket.private.h"
#include "csocket.h"

#include <memory>
using std::unique_ptr;

typedef std::set<CSocketPtr> CSockSet;

class LabelMatch;

class CMMSocketImpl;

class PendingSenderIROB;
class NetworkChooser;
class IROBSchedulingData;

class CSockMapping {
  public:
    // return true iff csock is suitable for these labels.
    /* must not be holding sk->scheduling_state_lock. */
    bool csock_matches(CSocket *csock, u_long send_label);
    bool csock_matches(CSocket *csock, u_long send_label, size_t num_bytes);

    CSocketPtr csock_with_labels(u_long send_label);
    CSocketPtr csock_with_labels(u_long send_label, size_t num_bytes);
    CSocketPtr connected_csock_with_labels(u_long send_label, bool locked=true);
    CSocketPtr connected_csock_with_labels(u_long send_label, size_t num_bytes,
                                           bool locked=true);
    CSocketPtr new_csock_with_labels(u_long send_label, bool locked=true);
    CSocketPtr new_csock_with_labels(u_long send_label, size_t bytes,
                                     bool locked=true);

    // tries to get a CSocket suitable for the given labels.
    //  prefers connected csocks, but will create a new one if necessary.
    // returns 0 on success.  On failure,
    //  returns CMM_UNDELIVERABLE if the labels contain an unsatisfiable 
    //  network restriction, or CMM_FAILED otherwise.
    int get_csock(PendingSenderIROB *psirob, CSocketPtr& csock);

    void remove_csock(CSocketPtr csock); // only removes, doesn't delete

    CSocketPtr get_idle_csock(bool grab_lock=true);

    bool get_local_iface_by_addr(struct in_addr addr, 
                                 struct net_interface& iface);
    bool get_iface_pair(u_long send_label, size_t num_bytes,
                        struct net_interface& local_iface,
                        struct net_interface& remote_iface);
    bool get_iface_pair_locked(u_long send_label, size_t num_bytes,
                               struct net_interface& local_iface,
                               struct net_interface& remote_iface);

    size_t count();
    size_t count_locked();
    size_t count_connected_locked();
    bool empty();
    
    bool can_satisfy_network_restrictions(u_long send_labels);

    void add_connection(int sock, 
                        struct in_addr local_addr,
                        struct net_interface remote_iface);

    /* append <mc_socket_t,osfd> pairs to this vector for each 
     * such mapping in this mc_socket. */
    void get_real_fds(mcSocketOsfdPairList &osfd_list);

    void setup(struct net_interface iface, bool local,
               bool make_connection, bool need_data_check);
    void teardown(struct net_interface iface, bool local);

    // only called when bootstrapping.
    void wait_for_connections();

    // only call when shutting down.
    // waits (pthread_join) for all workers to exit.
    void join_to_all_workers();

    void set_redundancy_strategy(int type);
    int get_redundancy_strategy();

    NetworkChooser *get_network_chooser();

    void pass_request_to_all_senders(PendingSenderIROB *psirob,
                                     const IROBSchedulingData& data);

    CSockMapping(CMMSocketImplPtr sk);
    ~CSockMapping();

    /* Functor must define int operator()(CSocketPtr), 
     * a function that returns -1 on error or >=0 on success */
    template <typename Functor>
    int for_each(Functor& f);
  private:
    struct AddRequestIfNotSent; // for passing scheduling requests to senders

    template <typename Functor>
    int for_each_locked(Functor& f);

    bool get_iface_pair_internal(u_long send_label, size_t num_bytes,
                                 struct net_interface& local_iface,
                                 struct net_interface& remote_iface,
                                 bool sockset_already_locked);
    bool get_iface_pair_locked_internal(u_long send_label, size_t num_bytes,
                                        struct net_interface& local_iface,
                                        struct net_interface& remote_iface,
                                        bool sockset_already_locked);

    bool csock_matches_internal(CSocket *csock, 
                                u_long send_label,
                                size_t num_bytes,
                                bool multisocket_already_locked,
                                bool sockset_already_locked);

    int redundancy_strategy_type;
    //RedundancyStrategy *redundancy_strategy;
    void check_redundancy(PendingSenderIROB *psirob);

    NetworkChooser *network_chooser;

    boost::weak_ptr<CMMSocketImpl> sk;  /* XXX: janky.  Remove later? */
    CSockSet available_csocks;
    RWLOCK_T sockset_mutex;

    struct get_worker_tids;

    CSocketPtr csock_by_ifaces(struct net_interface local_iface,
                               struct net_interface remote_iface,
                               bool grab_lock = true);
    CSocketPtr make_new_csocket(struct net_interface local_iface, 
                                struct net_interface remote_iface,
                                int accepted_sock = -1);
    bool get_iface(const NetInterfaceSet& ifaces, u_long label,
                   struct net_interface& iface, bool locked);
    bool get_remote_iface_by_addr(struct in_addr addr,
                                  struct net_interface& iface);
    bool get_iface_by_addr(const NetInterfaceSet& ifaces, struct in_addr addr,
                           struct net_interface& iface);

    template <typename Predicate>
    CSocketPtr find_csock(Predicate pred, bool grab_lock=true);

    friend struct GlobalLabelMatch;
};

template <typename Functor>
int CSockMapping::for_each(Functor& f)
{
    PthreadScopedRWLock lock(&sockset_mutex, false);
    return for_each_locked(f);
}

template <typename Functor>
int CSockMapping::for_each_locked(Functor& f)
{
    CSockSet::iterator it = available_csocks.begin();
    while (it != available_csocks.end()) {
        CSocketPtr csock = *it++;
        int rc = f(csock);
        if (rc < 0) {
            return rc;
        }
    }
    return 0;
}

template <typename Predicate>
CSocketPtr 
CSockMapping::find_csock(Predicate pred, bool grab_lock)
{
    unique_ptr<PthreadScopedRWLock> lock_ptr;
    if (grab_lock) {
        lock_ptr.reset(new PthreadScopedRWLock(&sockset_mutex, false));
    }

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
