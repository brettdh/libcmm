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
CMMSocketPassThrough::mc_reset()
{
    /* not meaningful on regular sockets */
    return 0;
}

int 
CMMSocketPassThrough::get_real_fds(mcSocketOsfdPairList &osfd_list)
{
    /* since these are never in the hash, this should never be called */
    assert(0);
    osfd_list.push_back(pair<mc_socket_t,int>(sock, sock));
    return 0;
}

void 
CMMSocketPassThrough::poll_map_back(struct pollfd *origfd, 
                                  const struct pollfd *realfd)
{
    /* since these are never in the hash, this should never be called */
    assert(0);
    origfd->revents = realfd->revents;
}


int 
CMMSocketPassThrough::prepare(u_long up_label)
{
    assert(0);
}

int 
CMMSocketPassThrough::setup(u_long up_label)
{
    assert(0);
}

void
CMMSocketPassThrough::teardown(u_long down_label)
{
    assert(0);
}

int 
CMMSocketPassThrough::non_blocking_connect(u_long initial_labels)
{
    assert(0);
}
