#include "cmm_socket.h"
#include "cmm_socket.private.h"

mc_socket_t
CMMSocket::create(int family, int type, int protocol)
{
  return CMMSocketImpl::create(family, type, protocol);
}

CMMSocketPtr
CMMSocket::lookup(mc_socket_t sock)
{
    return CMMSocketImpl::lookup(sock);
}

CMMSocketPtr
CMMSocket::lookup_by_irob(irob_id_t id)
{
    return CMMSocketImpl::lookup_by_irob(id);
}

int
CMMSocket::mc_close(mc_socket_t sock)
{
    return CMMSocketImpl::mc_close(sock);
}

void
CMMSocket::interface_down(struct net_interface down_iface)
{
    CMMSocketImpl::interface_down(down_iface);
}

void
CMMSocket::interface_up(struct net_interface up_iface)
{
    CMMSocketImpl::interface_up(up_iface);
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

int 
CMMSocket::mc_listen(int listener_sock, int backlog)
{
    return CMMSocketImpl::mc_listen(listener_sock, backlog);
}

mc_socket_t 
CMMSocket::mc_accept(int listener_sock, 
                     struct sockaddr *addr, socklen_t *addrlen)
{
    return CMMSocketImpl::mc_accept(listener_sock, addr, addrlen);
}

void
CMMSocket::cleanup()
{
    return CMMSocketImpl::cleanup();
}
