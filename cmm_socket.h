#ifndef cmm_socket_h
#define cmm_socket_h

#include <boost/shared_ptr.hpp>

#include "libcmm.h"
#include "libcmm_ipc.h"

class CMMSocket;

typedef boost::shared_ptr<CMMSocket> CMMSocketPtr;

class CMMSocket {
  public:
    static mc_socket_t create(int family, int type, int protocol);
    static CMMSocketPtr lookup(mc_socket_t sock);
    static int mc_close(mc_socket_t sock);

    static void interface_down(struct net_interface down_iface);
    static void interface_up(struct net_interface up_iface);

    virtual int reset() = 0;
    virtual int check_label(u_long label, resume_handler_t fn, void *arg) = 0;

    virtual int mc_connect(const struct sockaddr *serv_addr, 
                           socklen_t addrlen, 
                           u_long initial_labels) = 0;
    
    static int mc_select(mc_socket_t nfds, 
			 fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
			 struct timeval *timeout);
    static int mc_poll(struct pollfd fds[], nfds_t nfds, int timeout);

    static int mc_listen(int listener_sock, int backlog);
    static mc_socket_t mc_accept(int listener_sock, 
                                 struct sockaddr *addr, socklen_t *addrlen);

    virtual int mc_read(void *buf, size_t count) = 0;

    virtual int mc_getpeername(struct sockaddr *address, 
			       socklen_t *address_len) = 0;
    virtual int mc_getsockopt(int level, int optname, 
                              void *optval, socklen_t *optlen) = 0;
    virtual int mc_setsockopt(int level, int optname, 
                              const void *optval, socklen_t optlen) = 0;

    virtual ssize_t mc_send(const void *buf, size_t len, int flags,
                            u_long labels, resume_handler_t resume_handler, 
                            void *arg) = 0;
    virtual int mc_writev(const struct iovec *vec, int count,
                          u_long labels, resume_handler_t resume_handler, 
                          void *arg) = 0;

    virtual int mc_shutdown(int how) = 0;

    virtual ~CMMSocket() {}
};

#endif