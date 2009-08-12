#ifndef csocket_h_incl
#define csocket_h_incl

#include "tbb/concurrent_queue.h"
#include "cmm_socket_control.h"
#include <map>
#include <set>

#include "cmm_socket.private.h"

class CMMSocketSender;
class CMMSocketReceiver;
//class CMMSocketImpl;

class CSocketSender;
class CSocketReceiver;

struct ResumeOperation;

class CSocket {
  public:
    int osfd;
    CMMSocketImpl *sk;
    //CMMSocketSender *sendr;
    //CMMSocketReceiver *recvr;
    struct net_interface local_iface;
    struct net_interface remote_iface;

    CSocket(CMMSocketImpl *sk_, 
            struct net_interface local_iface_,
            struct net_interface remote_iface_,
            int accepted_sock = -1);
    ~CSocket();
    //void send(CMMSocketRequest req);
    void remove(void);

    bool matches(u_long send_labels, u_long recv_labels);

    int phys_connect(void);
  private:
    void startup_workers();
  
    friend class CMMSocketImpl;
    friend class CSocketSender;
    friend class CSocketReceiver;
    friend void resume_operation_thunk(ResumeOperation *op);
    
    /* worker threads */
    CSocketSender *csock_sendr;
    CSocketReceiver *csock_recvr;

    // indexes for the sender threads
    IROBSchedulingIndexes irob_indexes;
};


#endif
