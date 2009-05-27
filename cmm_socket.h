#ifndef cmm_socket_h
#define cmm_socket_h

#include <boost/shared_ptr.hpp>

#include "libcmm.h"

class CMMSocket;

typedef boost::shared_ptr<CMMSocket> CMMSocketPtr;

class CMMSocket {
  public:
    static mc_socket_t create(int family, int type, int protocol,
                              int cmm_flags);
    static CMMSocketPtr lookup(mc_socket_t sock);
    static int mc_close(mc_socket_t sock);

    static void put_label_down(u_long down_label);

    virtual int reset() = 0;
    virtual int check_label(u_long label, resume_handler_t fn, void *arg) = 0;

    virtual int mc_connect(const struct sockaddr *serv_addr, 
                           socklen_t addrlen, 
                           u_long initial_labels,
                           connection_event_cb_t label_down_cb,
                           connection_event_cb_t label_up_cb,
                           void *cb_arg) = 0;
    
    static int mc_select(mc_socket_t nfds, 
			 fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
			 struct timeval *timeout);
    static int mc_poll(struct pollfd fds[], nfds_t nfds, int timeout);

    virtual int mc_getpeername(struct sockaddr *address, 
			       socklen_t *address_len) = 0;
    virtual int mc_read(void *buf, size_t count) = 0;
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
