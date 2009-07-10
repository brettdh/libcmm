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

class CSocket {
  public:
    int osfd;
    CMMSocketImpl *sk;
    CMMSocketSender *sendr;
    CMMSocketReceiver *recvr;
    struct net_interface local_iface;
    struct net_interface remote_iface;

    CSocket(CMMSocketImpl *sk_, 
            CMMSocketSender *sendr_,
            CMMSocketReceiver *recvr_,
            struct net_interface local_iface_,
            struct net_interface remote_iface_,
            int accepted_sock = -1);
    ~CSocket();
    void send(CMMSocketRequest req);
  private:
    int phys_connect(void);
    
    CSocketSender *csock_sendr;
    CSocketReceiver *csock_recvr;
};


#endif
