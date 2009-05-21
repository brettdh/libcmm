#include "cmm_socket.h"
#include "cmm_socket.private.h"

CMMSocketPassThrough::CMMSocketPassThrough(mc_socket_t sock_)
{
    sock = sock_;
}

ssize_t 
CMMSocketPassThrough::mc_send(const void *buf, size_t len, int flags,
                              u_long labels, void (*resume_handler)(void*), 
                              void *arg)
{
    return send(sock, buf, len, flags);
}

int 
CMMSocketPassThrough::mc_writev(const struct iovec *vec, int count,
                                u_long labels, void (*resume_handler)(void*), 
                                void *arg)
{
    return writev(sock, vec, count);
}

int 
CMMSocketPassThrough::mc_getpeername(struct sockaddr *address, 
                                     socklen_t *address_len)
{
    return getpeername(sock, address, address_len);
}

int 
CMMSocketPassThrough::mc_read(void *buf, size_t count)
{
    return read(sock, buf, count);
}

int 
CMMSocketPassThrough::mc_connect(const struct sockaddr *serv_addr, 
                                 socklen_t addrlen_, 
                                 u_long initial_labels,
                                 connection_event_cb_t label_down_cb_,
                                 connection_event_cb_t label_up_cb_,
                                 void *cb_arg_)
{
    return connect(sock, serv_addr, addrlen_);
}

int 
CMMSocketPassThrough::mc_getsockopt(int level, int optname, 
                                    void *optval, socklen_t *optlen)
{
    return getsockopt(sock, level, optname, optval, optlen);
}

int
CMMSocketPassThrough::mc_setsockopt(int level, int optname, 
                                    const void *optval, socklen_t optlen)
{
    return setsockopt(sock, level, optname, optval, optlen);
}

int
CMMSocketPassThrough::mc_shutdown(int how)
{
    return shutdown(sock, how);
}

int 
CMMSocketPassThrough::reset()
{
    /* not meaningful on regular sockets */
    return 0;
}

int 
CMMSocketPassThrough::check_label(u_long label, resume_handler_t fn, void *arg)
{
    /* not meaningful on regular sockets */
    return 0;
}
