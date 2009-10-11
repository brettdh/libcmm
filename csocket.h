#ifndef csocket_h_incl
#define csocket_h_incl

#include "tbb/concurrent_queue.h"
#include "cmm_socket_control.h"
#include "cmm_socket.private.h"
#include <map>
#include <set>

//#include "cmm_socket.private.h"
#include "pending_irob.h"

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
    CMMSocketImplPtr sk;
    //CMMSocketSender *sendr;
    //CMMSocketReceiver *recvr;
    struct net_interface local_iface;
    struct net_interface remote_iface;

    static CSocketPtr create(boost::weak_ptr<CMMSocketImpl> sk_, 
                             struct net_interface local_iface_,
                             struct net_interface remote_iface_,
                             int accepted_sock = -1);
    ~CSocket();
    //void send(CMMSocketRequest req);
    //void remove(void);

    bool matches(u_long send_labels);

    int phys_connect(void);
    void startup_workers();

    // network measurements/estimates for this connection.
    u_long bandwidth();
    double RTT();
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
    
    /* worker threads */
    CSocketSender *csock_sendr;
    CSocketReceiver *csock_recvr;

    // only valid until the worker threads are created;
    // ensures that all CSocket pointers are shared
    boost::weak_ptr<CSocket> self_ptr;

    // indexes for the sender threads
    IROBSchedulingIndexes irob_indexes;
};

#endif
