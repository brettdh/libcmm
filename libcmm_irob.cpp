#include "libcmm_irob.h"

/* Begin an IROB on a multi-socket.  
 * Such IROBs have sender labels, receiver labels, and a thunk
 * associated, just as traditional multi-socket send operations do. */
irob_id_t begin_irob(mc_socket_t sock, int numdeps, const irob_id_t *deps, 
                     u_long send_labels, u_long recv_labels, 
                     resume_handler_t fn, void *arg)
{
    return CMMSocket::lookup(sock)->mc_begin_irob(numdeps, deps,
                                                  send_labels, recv_labels,
                                                  fn, arg);
}

/* End an IROB, notifying the receiver that it is complete. */
int end_irob(irob_id_t id)
{
    return CMMSocket::lookup_by_irob(id)->mc_end_irob(id);
}

/* Sending operations for IROBs.  Since an irob_id implies a socket,
 * the socket argument is not required. 
 * Recall that send operations within an IROB are guaranteed to be 
 * received in their original order (see receiver API below). */
ssize_t irob_send(irob_id_t irob_id, const void *buf, size_t len, int flags)
{
    return CMMSocket::lookup_by_irob(irob_id)->mc_irob_send(irob_id_,
                                                            buf, len, flags);
}

int irob_writev(irob_id_t irob_id, const struct iovec *vector, int count)
{
    return CMMSocket::lookup_by_irob(irob_id)->mc_irob_writev(irob_id_,
                                                              vector, count);
}
