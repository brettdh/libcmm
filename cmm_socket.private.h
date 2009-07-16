#ifndef cmm_socket_private_h
#define cmm_socket_private_h

#include "cmm_socket.h"
#include "common.h"

#include <map>
#include <vector>
#include <set>
#include <boost/shared_ptr.hpp>

class CMMSocketImpl;
typedef boost::shared_ptr<CMMSocketImpl> CMMSocketImplPtr;

class CSocket;
#include "csocket_mapping.h"

struct sockopt {
    void *optval;
    socklen_t optlen;

    sockopt() : optval(NULL), optlen(0) {}
};

/* < optname, (optval, optlen) > */
typedef std::map<int, struct sockopt> SockOptNames;

/* < level, < optname, (optval, optlen) > > */
typedef std::map<int, SockOptNames> SockOptHash;

#include "tbb/concurrent_hash_map.h"
typedef tbb::concurrent_hash_map<mc_socket_t, 
                                 CMMSocketImplPtr, 
                                 IntegerHashCompare<mc_socket_t> > CMMSockHash;

typedef tbb::concurrent_hash_map<irob_id_t, mc_socket_t, 
                                 IntegerHashCompare<irob_id_t> > IROBSockHash;

typedef tbb::concurrent_hash_map<int, 
                                 void*, /* unused; keys only, no values */
                                 IntegerHashCompare<int> > VanillaListenerSet;

typedef std::map<in_addr_t, struct net_interface> NetInterfaceMap;

class ListenerThread;
class CMMSocketSender;
class CMMSocketReceiver;

class CMMSocketImpl : public CMMSocket {
  public:
    static mc_socket_t create(int family, int type, int protocol);
    static CMMSocketPtr lookup(mc_socket_t sock);
    static CMMSocketPtr lookup_by_irob(irob_id_t id);
    static int mc_close(mc_socket_t sock);

    static void interface_down(struct net_interface down_iface);
    static void interface_up(struct net_interface up_iface);

    //virtual int check_label(u_long label, resume_handler_t fn, void *arg);

    virtual int mc_connect(const struct sockaddr *serv_addr,
                           socklen_t addrlen);
    virtual ssize_t mc_send(const void *buf, size_t len, int flags,
                            u_long send_labels, u_long recv_labels, 
                            resume_handler_t resume_handler, void *arg);
    virtual int mc_writev(const struct iovec *vec, int count,
                          u_long send_labels, u_long recv_labels, 
                          resume_handler_t resume_handler, void *arg);
    virtual int mc_shutdown(int how);

    virtual irob_id_t mc_begin_irob(int numdeps, const irob_id_t *deps, 
                                    u_long send_labels, u_long recv_labels,
                                    resume_handler_t rh, void *rh_arg);
    virtual int mc_end_irob(irob_id_t id);
    virtual ssize_t mc_irob_send(irob_id_t id, 
                                 const void *buf, size_t len, int flags);
    virtual int mc_irob_writev(irob_id_t id, 
                               const struct iovec *vector, int count);

    static int mc_select(mc_socket_t nfds, 
			 fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
			 struct timeval *timeout);
    static int mc_poll(struct pollfd fds[], nfds_t nfds, int timeout);

    static int mc_listen(int listener_sock, int backlog);
    static mc_socket_t mc_accept(int listener_sock, 
                                 struct sockaddr *addr, socklen_t *addrlen);

    virtual int mc_read(void *buf, size_t count, u_long *recv_labels);

    virtual int mc_getpeername(struct sockaddr *address, 
                               socklen_t *address_len);
    //virtual int reset();
    virtual int mc_getsockopt(int level, int optname, 
                              void *optval, socklen_t *optlen);
    virtual int mc_setsockopt(int level, int optname, 
                              const void *optval, socklen_t optlen);
    
    virtual ~CMMSocketImpl();

    static bool net_available(mc_socket_t sock, 
                              u_long send_labels, u_long recv_labels);
    
    void add_connection(int sock, 
                        struct in_addr local_addr,
                        struct in_addr remote_addr);
    
  private:
    friend class CSocket;
    friend class CSockMapping;
    friend class CMMSocketSender;
    friend class CMMSocketReceiver;

    static pthread_mutex_t hashmaps_mutex;
    static CMMSockHash cmm_sock_hash;
    static IROBSockHash irob_sock_hash;
    static VanillaListenerSet cmm_listeners;
    static NetInterfaceSet ifaces;

    virtual void setup(struct net_interface iface, bool local);
    virtual void teardown(struct net_interface iface, bool local);
    
    int set_all_sockopts(int osfd);

    void recv_remote_listener(int bootstrap_sock);
    void recv_remote_listeners(int bootstrap_sock);
    void send_local_listener(int bootstrap_sock, struct net_interface iface);
    void send_local_listeners(int bootstrap_sock);

    int connection_bootstrap(const struct sockaddr *remote_addr, 
                             socklen_t addrlen,
                             int bootstrap_sock = -1);

    //CSocket * get_readable_csock(CMMSockHash::const_accessor& ac);

    CMMSocketImpl(int family, int type, int protocol);

    mc_socket_t sock; /* file-descriptor handle for this multi-socket */

    CSockMapping csock_map;

    NetInterfaceSet local_ifaces;
    ListenerThread *listener_thread;

    CMMSocketSender *sendr;
    CMMSocketReceiver *recvr;

    /* these are used for connecting csockets */
    NetInterfaceSet remote_ifaces;
    in_port_t remote_listener_port; /* network byte order, 
                                     * recv'd from remote host */

    int non_blocking; /* 1 if non blocking, 0 otherwise */

    /* these are used for re-creating the socket */
    int sock_family;
    int sock_type;
    int sock_protocol;
    SockOptHash sockopts;

    irob_id_t next_irob;

    static int make_real_fd_set(int nfds, fd_set *fds,
                                mcSocketOsfdPairList &osfd_list, 
                                int *maxosfd);
    static int make_mc_fd_set(fd_set *fds, 
                              const mcSocketOsfdPairList &osfd_list);

    int get_osfd(u_long label);

    /* send a control message.
     * if osfd != -1, send it on that socket.
     * otherwise, pick any connection, creating one if needed. */
    void send_control_message(struct CMMSocketControlHdr hdr,
                              int osfd = -1);

    bool net_available(u_long send_labels, u_long recv_labels);

    /* shortcut utility functions for hashtable-based rwlocking.  */

    /* grab a readlock on this socket with the accessor. */
    void lock(CMMSockHash::const_accessor& ac);
    /* grab a writelock on this socket with the accessor. */
    void lock(CMMSockHash::accessor& ac);
};

class CMMSocketPassThrough : public CMMSocket {
  public:
    CMMSocketPassThrough(mc_socket_t sock_);

    virtual int mc_getpeername(struct sockaddr *address, 
                               socklen_t *address_len);
    //virtual int reset();
    //virtual int check_label(u_long label, resume_handler_t fn, void *arg);
    virtual int mc_read(void *buf, size_t count, u_long *recv_labels);
    virtual int mc_getsockopt(int level, int optname, 
                              void *optval, socklen_t *optlen);
    virtual int mc_setsockopt(int level, int optname, 
                              const void *optval, socklen_t optlen);

    virtual int mc_connect(const struct sockaddr *serv_addr,
                           socklen_t addrlen);
    virtual ssize_t mc_send(const void *buf, size_t len, int flags,
                            u_long send_labels, u_long recv_labels, 
                            resume_handler_t resume_handler, void *arg);
    virtual int mc_writev(const struct iovec *vec, int count,
                          u_long send_labels, u_long recv_labels, 
                          resume_handler_t resume_handler, void *arg);
    virtual int mc_shutdown(int how);

    virtual irob_id_t mc_begin_irob(int numdeps, const irob_id_t *deps, 
                                    u_long send_labels, u_long recv_labels,
                                    resume_handler_t rh, void *rh_arg);
    virtual int mc_end_irob(irob_id_t id);
    virtual ssize_t mc_irob_send(irob_id_t id, 
                                 const void *buf, size_t len, int flags);
    virtual int mc_irob_writev(irob_id_t id, 
                               const struct iovec *vector, int count);
  private:
    mc_socket_t sock;
};

//#define FAKE_SOCKET_MAGIC_LABELS 0xDECAFBAD

//void set_socket_labels(int osfd, u_long labels);

#endif /* include guard */
