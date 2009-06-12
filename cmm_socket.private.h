#ifndef cmm_socket_private_h
#define cmm_socket_private_h

#include "cmm_socket.h"
#include "common.h"

#include <map>
#include <vector>
#include <set>

struct csocket;
typedef std::map<u_long, std::map<u_long, struct csocket *> > CSockLabelMap;
typedef std::set<struct csocket *> CSockSet;

class CSockMapping {
  public:
    struct csocket * csock_with_send_label(u_long label);
    struct csocket * csock_with_recv_label(u_long label);
    struct csocket * csock_with_labels(u_long send_label, u_long recv_label);
    struct csocket * new_csock_with_labels(u_long send_label, u_long recv_label);
    void delete_csock_with_labels(u_long
    
  private:
    CSockLabelMap csocks_by_send_label;
    CSockLabelMap csocks_by_recv_label;
};

struct sockopt {
    void *optval;
    socklen_t optlen;

    sockopt() : optval(NULL), optlen(0) {}
};

/* < optname, (optval, optlen) > */
typedef std::map<int, struct sockopt> SockOptNames;

/* < level, < optname, (optval, optlen) > > */
typedef std::map<int, SockOptNames> SockOptHash;

class CMMSocketImpl;
typedef boost::shared_ptr<CMMSocketImpl> CMMSocketImplPtr;

#include "tbb/concurrent_hash_map.h"
typedef tbb::concurrent_hash_map<mc_socket_t, 
                                 CMMSocketImplPtr, 
                                 MyHashCompare<mc_socket_t> > CMMSockHash;

typedef tbb::concurrent_hash_map<int, 
                                 void*, /* unused; keys only, no values */
                                 MyHashCompare<int> > ListenerSockSet;

typedef std::vector<std::pair<mc_socket_t, int> > mcSocketOsfdPairList;

class CMMSocketImpl : public CMMSocket {
  public:
    static mc_socket_t create(int family, int type, int protocol);
    static CMMSocketPtr lookup(mc_socket_t sock);
    static int mc_close(mc_socket_t sock);

    static void put_label_down(u_long down_label);
    virtual int check_label(u_long label, resume_handler_t fn, void *arg);

    virtual int mc_connect(const struct sockaddr *serv_addr, socklen_t addrlen,
                           u_long initial_labels);
    virtual ssize_t mc_send(const void *buf, size_t len, int flags,
                            u_long labels, resume_handler_t resume_handler, 
                            void *arg);
    virtual int mc_writev(const struct iovec *vec, int count,
                          u_long labels, resume_handler_t resume_handler, 
                          void *arg);    
    virtual int mc_shutdown(int how);

    static int mc_select(mc_socket_t nfds, 
			 fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
			 struct timeval *timeout);
    static int mc_poll(struct pollfd fds[], nfds_t nfds, int timeout);

    static int mc_listen(int listener_sock, int backlog);
    static mc_socket_t mc_accept(int listener_sock, 
                                 struct sockaddr *addr, socklen_t *addrlen);

    virtual int mc_read(void *buf, size_t count);

    virtual int mc_getpeername(struct sockaddr *address, 
                               socklen_t *address_len);
    virtual int reset();
    virtual int mc_getsockopt(int level, int optname, 
                              void *optval, socklen_t *optlen);
    virtual int mc_setsockopt(int level, int optname, 
                              const void *optval, socklen_t optlen);
    
    virtual ~CMMSocketImpl();
    
  private:
    static CMMSockHash cmm_sock_hash;
    static ListenerSockSet cmm_listeners;

    /* check whether this label is available; if not, 
     * register the thunk or return an error if no thunk given */
    int preapprove(u_long labels, 
                   resume_handler_t resume_handler, void *arg);


    virtual int non_blocking_connect(u_long initial_labels);

    /* make sure that the socket is ready to send data with up_label. */
    virtual int prepare(u_long up_label);

    //virtual int setup(u_long up_label) = 0;
    virtual void teardown(u_long down_label);
    
    int set_all_sockopts(int osfd);

    /* append <mc_socket_t,osfd> pairs to this vector for each 
     * such mapping in this mc_socket. 
     * Returns 0 if all the mappings were appended, 
     *        -1 if there were no connected osfds. */
    virtual int get_real_fds(mcSocketOsfdPairList &osfd_list);

    struct csocket * get_readable_csock(CMMSockHash::const_accessor& ac);

    CMMSocketImpl(int family, int type, int protocol);

    mc_socket_t sock;
    CSockMapping csocks;
    CSockSet connected_csocks;

    int non_blocking; /* 1 if non blocking, 0 otherwise*/

    /* these are used for re-creating the socket */
    int sock_family;
    int sock_type;
    int sock_protocol;
    SockOptHash sockopts;

    /* these are used for reconnecting the socket */
    struct sockaddr *addr; 
    socklen_t addrlen;

#if 0 /*def IMPORT_RULES */
    /* In the absence of label_up callbacks, I think this is unnecessary too. */
    int connecting; /* true iff the cmm_socket is currently in the process
		     * of calling any label_up callback.  Used to ensure
		     * we don't accidentally pick a "superior" label
		     * in this case. */
#endif
    static int make_real_fd_set(int nfds, fd_set *fds,
                                mcSocketOsfdPairList &osfd_list, 
                                int *maxosfd);
    static int make_mc_fd_set(fd_set *fds, 
                              const mcSocketOsfdPairList &osfd_list);

    int get_osfd(u_long label);
};

class CMMSocketPassThrough : public CMMSocket {
  public:
    CMMSocketPassThrough(mc_socket_t sock_);

    virtual int mc_getpeername(struct sockaddr *address, 
                               socklen_t *address_len);
    virtual int reset();
    virtual int check_label(u_long label, resume_handler_t fn, void *arg);
    virtual int mc_read(void *buf, size_t count);
    virtual int mc_getsockopt(int level, int optname, 
                              void *optval, socklen_t *optlen);
    virtual int mc_setsockopt(int level, int optname, 
                              const void *optval, socklen_t optlen);

    virtual int mc_connect(const struct sockaddr *serv_addr, socklen_t addrlen,
                           u_long initial_labels);
    virtual ssize_t mc_send(const void *buf, size_t len, int flags,
                            u_long labels, resume_handler_t resume_handler, 
                            void *arg);
    virtual int mc_writev(const struct iovec *vec, int count,
                          u_long labels, resume_handler_t resume_handler, 
                          void *arg);
    virtual int mc_shutdown(int how);
  private:
    mc_socket_t sock;
};

#define FAKE_SOCKET_MAGIC_LABELS 0xDECAFBAD

void set_socket_labels(int osfd, u_long labels);

#endif /* include guard */
