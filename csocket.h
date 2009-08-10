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
    //CMMSocketSender *sendr;
    //CMMSocketReceiver *recvr;
    struct net_interface local_iface;
    struct net_interface remote_iface;

    CSocket(CMMSocketImpl *sk_, 
            CMMSocketSender *sendr_,
            CMMSocketReceiver *recvr_,
            struct net_interface local_iface_,
            struct net_interface remote_iface_,
            int accepted_sock = -1);
    ~CSocket();
    //void send(CMMSocketRequest req);
    void remove(void);

    int phys_connect(void);
  private:
    void startup_workers();
  
    friend class CSocketSender;
    friend class CSocketReceiver;
    
    /* worker threads */
    CSocketSender *csock_sendr;
    CSocketReceiver *csock_recvr;

    // indexes for the sender threads
    std::set<irob_id_t> new_irobs;
    std::set<struct irob_chunk_data *> new_chunks;
    std::set<irob_id_t> finished_irobs;

    std::set<irob_id_t> unacked_irobs;
    std::map<irob_id_t, std::set<u_long> > unacked_chunks;
};


#endif
