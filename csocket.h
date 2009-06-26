#ifndef csocket_h_incl
#define csocket_h_incl

struct csocket {
    int osfd;
    struct net_interface local_iface;
    struct net_interface remote_iface;
    CMMSocketImpl *msock;

    csocket(CMMSocketImpl *msock_, struct net_interface local_iface_,
            struct net_interface remote_iface_);
    int phys_connect(void);
    ~csocket();
};

typedef std::map<u_long, std::map<u_long, struct csocket *> > CSockLabelMap;
typedef std::set<struct csocket *> CSockSet;

class LabelMatch;

class CSockMapping {
  public:
    struct csocket * csock_with_send_label(u_long label);
    struct csocket * csock_with_recv_label(u_long label);
    struct csocket * csock_with_labels(u_long send_label, u_long recv_label);
    struct csocket * new_csock_with_labels(u_long send_label, u_long recv_label);

    void delete_csock_with_labels(struct csocket *csock);
    struct csocket * lookup(int fd);
    
    CSockMapping(CMMSocketImpl *sk);
  private:
    //CSockLabelMap csocks_by_send_label;
    //CSockLabelMap csocks_by_recv_label;
    CMMSocketImplPtr sk;  /* XXX: janky.  Remove later. */

    bool get_local_iface(u_long label, struct net_interface& iface);
    bool get_remote_iface(u_long label, struct net_interface& iface);
    bool get_iface(const NetInterfaceList& ifaces, u_long label,
                   struct net_interface& iface);
    struct csocket * find_csock(const LabelMatch& pred);
};

#endif
