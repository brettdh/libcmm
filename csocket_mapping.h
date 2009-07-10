#ifndef csocket_mapping_h_incl
#define csocket_mapping_h_incl

#include <set>
#include <map>
#include <netinet/in.h>
#include <sys/types.h>
#include "common.h"

class CSocket;
typedef std::set<CSocket *> CSockSet;

class LabelMatch;

typedef std::map<u_long, std::map<u_long, CSocket *> > CSockLabelMap;

class CMMSocketImpl;

class CSockMapping {
  public:
    CSocket * csock_with_send_label(u_long label);
    CSocket * csock_with_recv_label(u_long label);
    CSocket * csock_with_labels(u_long send_label, u_long recv_label);
    CSocket * new_csock_with_labels(u_long send_label, u_long recv_label);
    void delete_csock_with_labels(CSocket *csock);
    CSocket * lookup(int fd);
    
    void add_connection(int sock, 
                        struct in_addr local_addr,
                        struct in_addr remote_addr);

    CSockMapping(CMMSocketImpl *sk);
  private:
    friend class CMMSocketImpl;

    //CSockLabelMap csocks_by_send_label;
    //CSockLabelMap csocks_by_recv_label;
    CMMSocketImpl *sk;  /* XXX: janky.  Remove later? */
    CSockSet connected_csocks;

    bool get_local_iface(u_long label, struct net_interface& iface);
    bool get_remote_iface(u_long label, struct net_interface& iface);
    bool get_iface(const NetInterfaceSet& ifaces, u_long label,
                   struct net_interface& iface);
    bool get_local_iface_by_addr(struct in_addr addr, 
                                 struct net_interface& iface);
    bool get_remote_iface_by_addr(struct in_addr addr,
                                  struct net_interface& iface);
    bool get_iface_by_addr(const NetInterfaceSet& ifaces, struct in_addr addr,
                           struct net_interface& iface);

    template <typename Predicate>
    CSocket * find_csock(Predicate pred) {
        CSockSet::const_iterator it = find_if(connected_csocks.begin(), 
                                              connected_csocks.end(), 
                                              pred);
        if (it == connected_csocks.end()) {
            return NULL;
        } else {
            return *it;
        }
    }
};

#endif
