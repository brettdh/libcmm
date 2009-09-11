#include "cmm_socket.h"
#include "cmm_socket.private.h"

CMMSocketPassThrough::CMMSocketPassThrough(mc_socket_t sock_)
{
    sock = sock_;
}

ssize_t 
CMMSocketPassThrough::mc_send(const void *buf, size_t len, int flags,
                              u_long send_labels, 
                              resume_handler_t rh, void *arg)
{
    return send(sock, buf, len, flags);
}

int 
CMMSocketPassThrough::mc_writev(const struct iovec *vec, int count,
                                u_long send_labels, 
                                resume_handler_t rh, void *arg)
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
CMMSocketPassThrough::mc_read(void *buf, size_t count, u_long *recv_labels)
{
    return read(sock, buf, count);
}

int 
CMMSocketPassThrough::mc_connect(const struct sockaddr *serv_addr, 
                                 socklen_t addrlen_)
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

irob_id_t
CMMSocketPassThrough::mc_begin_irob(int numdeps, const irob_id_t *deps, 
                                    u_long send_labels, 
                                    resume_handler_t rh, void *rh_arg)
{
    errno = EBADF;
    return -1;
}

int
CMMSocketPassThrough::mc_end_irob(irob_id_t id)
{
    errno = EBADF;
    return -1;
}

ssize_t 
CMMSocketPassThrough::mc_irob_send(irob_id_t id, 
                                   const void *buf, size_t len, int flags)
{
    errno = EBADF;
    return -1;
}

int 
CMMSocketPassThrough::mc_irob_writev(irob_id_t id, 
                                     const struct iovec *vector, int count)
{
    errno = EBADF;
    return -1;
}
