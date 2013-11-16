#include "cmm_socket.h"
#include "cmm_socket.private.h"
#include "debug.h"
#include <errno.h>

static void passthrough_debug_alert(const char *fn)
{
    //dbgprintf("Passthrough socket function called: %s\n", fn);
}

CMMSocketPassThrough::CMMSocketPassThrough(mc_socket_t sock_)
{
    sock = sock_;
}

ssize_t 
CMMSocketPassThrough::mc_send(const void *buf, size_t len, int flags,
                              u_long send_labels, 
                              resume_handler_t rh, void *arg)
{
    if (flags == 0) {
        passthrough_debug_alert("cmm_write");
        return write(sock, buf, len);
    } else {
        passthrough_debug_alert("cmm_send");
        return send(sock, buf, len, flags);
    }
}

int 
CMMSocketPassThrough::mc_writev(const struct iovec *vec, int count,
                                u_long send_labels, 
                                resume_handler_t rh, void *arg)
{
    passthrough_debug_alert("cmm_writev");
    return writev(sock, vec, count);
}

int 
CMMSocketPassThrough::mc_getpeername(struct sockaddr *address, 
                                     socklen_t *address_len)
{
    passthrough_debug_alert("cmm_getpeername");
    return getpeername(sock, address, address_len);
}

int 
CMMSocketPassThrough::mc_getsockname(struct sockaddr *address, 
                                     socklen_t *address_len)
{
    passthrough_debug_alert("cmm_getsockname");
    return getsockname(sock, address, address_len);
}

int 
CMMSocketPassThrough::mc_recv(void *buf, size_t count, int flags,
                              u_long *recv_labels)
{
    if (flags == 0) {
        passthrough_debug_alert("cmm_read");
        return read(sock, buf, count);
    } else {
        passthrough_debug_alert("cmm_recv");
        return recv(sock, buf, count, flags);
    }
}

int 
CMMSocketPassThrough::mc_connect(const struct sockaddr *serv_addr, 
                                 socklen_t addrlen_)
{
    passthrough_debug_alert("cmm_connect");
    return connect(sock, serv_addr, addrlen_);
}

int 
CMMSocketPassThrough::mc_getsockopt(int level, int optname, 
                                    void *optval, socklen_t *optlen)
{
    passthrough_debug_alert("cmm_getsockopt");
    return getsockopt(sock, level, optname, optval, optlen);
}

int
CMMSocketPassThrough::mc_setsockopt(int level, int optname, 
                                    const void *optval, socklen_t optlen)
{
    passthrough_debug_alert("cmm_setsockopt");
    return setsockopt(sock, level, optname, optval, optlen);
}

int
CMMSocketPassThrough::mc_shutdown(int how)
{
    passthrough_debug_alert("cmm_shutdown");
    return shutdown(sock, how);
}

void
CMMSocketPassThrough::mc_interrupt_waiters()
{
    passthrough_debug_alert("cmm_interrupt_waiters");
}

irob_id_t
CMMSocketPassThrough::mc_begin_irob(int numdeps, const irob_id_t *deps, 
                                    u_long send_labels, 
                                    resume_handler_t rh, void *rh_arg)
{
    passthrough_debug_alert("cmm_begin_irob");
    errno = EBADF;
    return -1;
}

int
CMMSocketPassThrough::mc_end_irob(irob_id_t id)
{
    passthrough_debug_alert("cmm_end_irob");
    errno = EBADF;
    return -1;
}

ssize_t 
CMMSocketPassThrough::mc_irob_send(irob_id_t id, 
                                   const void *buf, size_t len, int flags)
{
    passthrough_debug_alert("cmm_irob_send");
    errno = EBADF;
    return -1;
}

int 
CMMSocketPassThrough::mc_irob_writev(irob_id_t id, 
                                     const struct iovec *vector, int count)
{
    passthrough_debug_alert("cmm_irob_writev");
    errno = EBADF;
    return -1;
}

int
CMMSocketPassThrough::irob_relabel(irob_id_t id, u_long new_labels)
{
    passthrough_debug_alert("irob_relabel");
    errno = EBADF;
    return -1;
}

int 
CMMSocketPassThrough::mc_get_failure_timeout(u_long label, struct timespec *ts)
{
    passthrough_debug_alert("cmm_get_failure_timeout");
    errno = EBADF;
    return -1;    
}

int 
CMMSocketPassThrough::mc_set_failure_timeout(u_long label, const struct timespec *ts)
{
    passthrough_debug_alert("cmm_set_failure_timeout");
    errno = EBADF;
    return -1;
}

int
CMMSocketPassThrough::mc_num_networks()
{
    // This is a real TCP socket, so just one network.
    // It's probably an error to call this, because
    //  if you're not calling it on a multisocket, 
    //  the answer is obviously 1.
    return 1;
}


intnw_network_strategy_t
CMMSocketPassThrough::mc_get_network_strategy(instruments_context_t ctx, u_long net_restriction_labels)
{
    return nullptr;
}

void
CMMSocketPassThrough::mc_free_network_strategy(intnw_network_strategy_t)
{
}

double
CMMSocketPassThrough::mc_estimate_transfer_time(intnw_network_strategy_t strategy, u_long labels, size_t datalen)
{
    return 0.0;
}

double
CMMSocketPassThrough::mc_estimate_transfer_energy(intnw_network_strategy_t strategy, u_long labels, size_t datalen)
{
    return 0.0;
}

double
CMMSocketPassThrough::mc_estimate_transfer_data(intnw_network_strategy_t strategy, u_long labels, size_t datalen)
{
    return 0.0;
}

double
CMMSocketPassThrough::mc_get_oldest_irob_delay()
{
    return 0.0;
}

instruments_estimator_t 
CMMSocketPassThrough::mc_get_rtt_estimator(u_long net_restriction_labels)
{
    return nullptr;
}

