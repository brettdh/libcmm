#ifndef csocket_h_incl
#define csocket_h_incl

#include "tbb/concurrent_queue.h"
#include "cmm_socket_control.h"
#include <map>
#include <set>

class CMMSocketSender;
class CMMSocketReceiver;
class CMMSocketImpl;

class CSocketSender;
class CSocketReceiver;

class CSocket {
    int osfd;
    CMMSocketImpl *msock;
    CMMSocketSender *sendr;
    CMMSocketReceiver *recvr;
    struct net_interface local_iface;
    struct net_interface remote_iface;

    CSocket(CMMSocketImpl *msock_, 
            CMMSocketSender *sendr_,
            CMMSocketReceiver *recvr_,
            struct net_interface local_iface_,
            struct net_interface remote_iface_,
            in_port_t remote_listener_port_);
    ~CSocket();
    void send(struct CMMSocketControlHdr hdr);
  private:
    int phys_connect(void);
    
    CSocketSender *csock_sendr;
    CSocketReceiver *csock_recvr;
};

typedef std::map<u_long, std::map<u_long, CSocket *> > CSockLabelMap;
typedef std::set<CSocket *> CSockSet;

class LabelMatch;

class CSockMapping {
  public:
    CSocket * csock_with_send_label(u_long label);
    CSocket * csock_with_recv_label(u_long label);
    CSocket * csock_with_labels(u_long send_label, u_long recv_label);
    CSocket * new_csock_with_labels(u_long send_label, u_long recv_label);

    void delete_csock_with_labels(CSocket *csock);
    CSocket * lookup(int fd);
    
    CSockMapping(CMMSocketImpl *sk);
  private:
    //CSockLabelMap csocks_by_send_label;
    //CSockLabelMap csocks_by_recv_label;
    CMMSocketImplPtr sk;  /* XXX: janky.  Remove later? */

    bool get_local_iface(u_long label, struct net_interface& iface);
    bool get_remote_iface(u_long label, struct net_interface& iface);
    bool get_iface(const NetInterfaceList& ifaces, u_long label,
                   struct net_interface& iface);
    CSocket * find_csock(const LabelMatch& pred);
};

#endif
