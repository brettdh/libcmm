#ifndef csocket_h_incl
#define csocket_h_incl

#include "cmm_socket_control.h"
#include "cmm_socket.private.h"
#include <map>
#include <set>

//#include "cmm_socket.private.h"
#include "pending_irob.h"

#include "net_stats.h"

//class CMMSocketSender;
//class CMMSocketReceiver;
class CMMSocketImpl;

class CSocketSender;
class CSocketReceiver;

struct ResumeOperation;

#include <boost/shared_ptr.hpp>
#include <boost/weak_ptr.hpp>

class CSocket;
typedef boost::shared_ptr<CSocket> CSocketPtr;

class CSocket {
  public:
    int osfd;
    int oserr; // if non-zero, there was an error.
    CMMSocketImplPtr sk;
    //CMMSocketSender *sendr;
    //CMMSocketReceiver *recvr;
    struct net_interface local_iface;
    struct net_interface remote_iface;
    NetStats stats;

    static CSocketPtr create(boost::weak_ptr<CMMSocketImpl> sk_, 
                             struct net_interface local_iface_,
                             struct net_interface remote_iface_,
                             int accepted_sock = -1);
    ~CSocket();
    //void send(CMMSocketRequest req);
    //void remove(void);

    /* must not be holding sk->scheduling_state_lock. */
    bool matches(u_long send_labels);

    /* shortcut for (matches(CMM_LABEL_ONDEMAND|CMM_LABEL_SMALL) ||
     *               matches(CMM_LABEL_ONDEMAND|CMM_LABEL_LARGE))
     * must not be holding sk->scheduling_state_lock. */
    bool is_fg();

    // return true iff this is the only connection possible 
    // right now (used for trickling background data).
    bool only_connection();

    void startup_workers();
    bool is_connected();
    int wait_until_connected();

    // network measurements/estimates for this connection.
    u_long bandwidth();
    double RTT();
    struct timespec retransmission_timeout();
    ssize_t trickle_chunksize();
  private:
    // only allow shared_ptr creation
    CSocket(boost::weak_ptr<CMMSocketImpl> sk_, 
            struct net_interface local_iface_,
            struct net_interface remote_iface_,
            int accepted_sock);

    friend class CSockMapping;
    friend class CMMSocketImpl;
    friend class CSocketSender;
    friend class CSocketReceiver;
    friend void resume_operation_thunk(ResumeOperation *op);
    
    int phys_connect(void);

    /* worker threads */
    CSocketSender *csock_sendr;
    CSocketReceiver *csock_recvr;

    struct timeval last_fg;
    void update_last_fg();

    // for coordination between this CSocket's worker threads
    pthread_mutex_t csock_lock;
    pthread_cond_t csock_cv;
    bool connected;

    // only valid until the worker threads are created;
    // ensures that all CSocket pointers are shared
    boost::weak_ptr<CSocket> self_ptr;

    // indexes for the sender threads
    IROBSchedulingIndexes irob_indexes;
};

#endif
