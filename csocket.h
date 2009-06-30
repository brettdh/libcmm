#ifndef csocket_h_incl
#define csocket_h_incl

class CSocket {
    int osfd;
    CMMSocketReceiver *recvr;
    struct net_interface local_iface;
    struct net_interface remote_iface;
    in_port_t remote_listener_port;

    CSocket(CMMSocketImpl *msock_, 
            struct net_interface local_iface_,
            struct net_interface remote_iface_,
            in_port_t remote_listener_port_);
    ~CSocket();

    int phys_connect(void);

    void RunReceiver();
  private:
    pthread_t listener;

    typedef bool (CSocket::*dispatch_fn_t)(struct CMMSocketControlHdr);
    static std::map<short, CSocket::dispatch_fn_t> dispatcher;

    bool dispatch(struct CMMSocketControlHdr);

    bool pass_header(struct CMMSocketControlHdr);
    bool pass_header_and_data(struct CMMSocketControlHdr);
    bool unrecognized_control_msg(struct CMMSocketControlHdr);
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
