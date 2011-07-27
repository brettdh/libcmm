#ifndef csocket_h_incl
#define csocket_h_incl

#include "cmm_socket_control.h"
//#include "cmm_socket.private.h"
#include <map>
#include <set>

//#include "cmm_socket.private.h"
#include "pending_irob.h"
#include "irob_scheduling.h"

#include "net_stats.h"

//class CMMSocketSender;
//class CMMSocketReceiver;
class CMMSocketImpl;

class CSocketSender;
class CSocketReceiver;

struct ResumeOperation;

#include <boost/shared_ptr.hpp>
#include <boost/weak_ptr.hpp>

#include <arpa/inet.h>
#include <netinet/in.h>

class CSocket;
typedef boost::shared_ptr<CSocket> CSocketPtr;

class CSocket {
  public:
    int osfd;
    int oserr; // if non-zero, there was an error.
    boost::shared_ptr<CMMSocketImpl> sk;
    //CMMSocketSender *sendr;
    //CMMSocketReceiver *recvr;
    struct net_interface local_iface;
    struct net_interface remote_iface;
#ifndef CMM_UNIT_TESTING
    NetStats stats;

    static CSocketPtr create(boost::weak_ptr<CMMSocketImpl> sk_, 
                             struct net_interface local_iface_,
                             struct net_interface remote_iface_,
                             int accepted_sock = -1);
#endif
#ifdef CMM_UNIT_TESTING
    CSocket(struct net_interface local_iface_,
            struct net_interface remote_iface_);
#else
    ~CSocket();
    //void send(CMMSocketRequest req);
    //void remove(void);

    /* must not be holding sk->scheduling_state_lock. */
    bool matches(u_long send_labels);

    /* shortcut for (matches(CMM_LABEL_ONDEMAND|CMM_LABEL_SMALL) ||
     *               matches(CMM_LABEL_ONDEMAND|CMM_LABEL_LARGE))
     * must not be holding sk->scheduling_state_lock. */
    bool is_fg();
    bool is_fg_ignore_trouble();

    // return true iff the csocket is busy sending app data
    bool is_busy();

    // return true iff this is the only connection possible 
    // right now (used for trickling background data).
    bool only_connection();

    void startup_workers();
    bool is_connected();
    int wait_until_connected();

    // called when a new incoming connection is added
    //  by the listener.
    // The CSocketReceiver calls this in order to let
    //  the listener thread continue.
    int send_confirmation();

    // network measurements/estimates for this connection.
    u_long bandwidth();
    double RTT();
    struct timespec retransmission_timeout();
    ssize_t trickle_chunksize();

    long int tcp_rto();
    void print_tcp_rto();
    
    struct timeval get_last_fg();

    struct net_interface bottleneck_iface();

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

    struct timeval last_app_data_sent;
    void update_last_app_data_sent();

    struct timespec last_trouble_check;

    // for coordination between this CSocket's worker threads
    pthread_mutex_t csock_lock;
    pthread_cond_t csock_cv;
    bool connected;

    // to distinguish between connecting and accepting sockets
    bool accepting;
    
    // returns true iff FG traffic shouldn't be sent on this network
    //  because we think it might be disconnected, but
    //  we haven't been notified as such by the scout yet.
    bool is_in_trouble();
    
    // return true if this CSocket's TCP connection has
    //  unACKed bytes in flight.
    bool data_inflight();

    // return a relative timeout representing
    // the earliest time that you'd want to check 
    // whether this network is in trouble.
    struct timespec trouble_check_timeout();

    // only valid until the worker threads are created;
    // ensures that all CSocket pointers are shared
    boost::weak_ptr<CSocket> self_ptr;

    // indexes for the sender threads
    IROBSchedulingIndexes irob_indexes;

    // true when I'm sending app data, or begin/end irob msg
    bool busy;
#endif // ifndef CMM_UNIT_TESTING
};

#endif
