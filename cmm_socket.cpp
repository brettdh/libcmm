#include "cmm_socket.h"
#include "cmm_socket.private.h"

/* TODO: add parallel option */
mc_socket_t
CMMSocket::create(int family, int type, int protocol, int cmm_flags)
{
  return CMMSocketImpl::create(family, type, protocol, cmm_flags);
}

CMMSocketPtr
CMMSocket::lookup(mc_socket_t sock)
{
    return CMMSocketImpl::lookup(sock);
}

int
CMMSocket::mc_close(mc_socket_t sock)
{
    return CMMSocketImpl::mc_close(sock);
}

void
CMMSocket::put_label_down(u_long down_label)
{
    CMMSocketImpl::put_label_down(down_label);
}

int 
CMMSocket::mc_select(mc_socket_t nfds, 
		     fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
		     struct timeval *timeout)
{
    return CMMSocketImpl::mc_select(nfds, readfds, writefds, 
				    exceptfds, timeout);
}

int 
CMMSocket::mc_poll(struct pollfd fds[], nfds_t nfds, int timeout)
{
    return CMMSocketImpl::mc_poll(fds, nfds, timeout);
}
