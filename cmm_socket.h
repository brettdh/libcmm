#ifndef cmm_socket_h
#define cmm_socket_h

#include <boost/shared_ptr.hpp>

#include "libcmm.h"
#include "libcmm_ipc.h"
#include "libcmm_irob.h"

class CMMSocket;

typedef boost::shared_ptr<CMMSocket> CMMSocketPtr;

/* This class is a pure virtual interface to allow simple 
 * lookup-and-passthrough for multi-socket calls on regular sockets.
 * See how CMMSocket::lookup is used in libcmm.cpp, and
 * see also CMMSocketPassthrough in cmm_socket.private.h. */

class CMMSocket {
  public:
    static mc_socket_t create(int family, int type, int protocol);
    static CMMSocketPtr lookup(mc_socket_t sock);
    static CMMSocketPtr lookup_by_irob(irob_id_t id);
    static int mc_close(mc_socket_t sock);

    static void interface_down(struct net_interface down_iface);
    static void interface_up(struct net_interface up_iface);

    // cmm_close all remaining mc_sockets.
    static void cleanup();

    //virtual int reset() = 0;
    //virtual int check_label(u_long label, resume_handler_t fn, 
    //                        void *arg) = 0;

    virtual int mc_connect(const struct sockaddr *serv_addr, 
                           socklen_t addrlen) = 0;
    
    static int mc_select(mc_socket_t nfds, 
			 fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
			 struct timeval *timeout);
    static int mc_poll(struct pollfd fds[], nfds_t nfds, int timeout);

    static int mc_listen(int listener_sock, int backlog);
    static mc_socket_t mc_accept(int listener_sock, 
                                 struct sockaddr *addr, socklen_t *addrlen);

    virtual int mc_recv(void *buf, size_t count, int flags,
                        u_long *recv_labels) = 0;

    virtual int mc_getpeername(struct sockaddr *address, 
			       socklen_t *address_len) = 0;
    virtual int mc_getsockname(struct sockaddr *address, 
			       socklen_t *address_len) = 0;
    virtual int mc_getsockopt(int level, int optname, 
                              void *optval, socklen_t *optlen) = 0;
    virtual int mc_setsockopt(int level, int optname, 
                              const void *optval, socklen_t optlen) = 0;

    virtual ssize_t mc_send(const void *buf, size_t len, int flags,
                            u_long send_labels, 
                            resume_handler_t resume_handler, void *arg) = 0;
    virtual int mc_writev(const struct iovec *vec, int count,
                          u_long send_labels, 
                          resume_handler_t resume_handler, void *arg) = 0;

    virtual int mc_shutdown(int how) = 0;
    
    virtual irob_id_t mc_begin_irob(int numdeps, const irob_id_t *deps, 
                                    u_long send_labels, 
                                    resume_handler_t rh, void *rh_arg) = 0;
    virtual int mc_end_irob(irob_id_t id) = 0;
    virtual ssize_t mc_irob_send(irob_id_t id, 
                                 const void *buf, size_t len, int flags) = 0;
    virtual int mc_irob_writev(irob_id_t id, 
                               const struct iovec *vector, int count) = 0;
    virtual int irob_relabel(irob_id_t id, u_long new_labels) = 0;

    virtual int mc_get_failure_timeout(u_long label, struct timespec *ts) = 0;
    virtual int mc_set_failure_timeout(u_long label, const struct timespec *ts) = 0;

    virtual ~CMMSocket() {}
};

#endif
