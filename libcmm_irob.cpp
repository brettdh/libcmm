#include "libcmm_private.h"
#include "libcmm_irob.h"
#include "cmm_socket.h"
#include "cmm_socket.private.h"
#include <errno.h>

/* Begin an IROB on a multi-socket.  
 * Such IROBs have sender labels, receiver labels, and a thunk
 * associated, just as traditional multi-socket send operations do. */
irob_id_t begin_irob(mc_socket_t sock, int numdeps, const irob_id_t *deps, 
                     u_long send_labels, 
                     resume_handler_t fn, void *arg)
{
    return CMMSocket::lookup(sock)->mc_begin_irob(numdeps, deps,
                                                  send_labels,
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
    return CMMSocket::lookup_by_irob(irob_id)->mc_irob_send(irob_id,
                                                            buf, len, flags);
}

int irob_writev(irob_id_t irob_id, const struct iovec *vector, int count)
{
    return CMMSocket::lookup_by_irob(irob_id)->mc_irob_writev(irob_id,
                                                              vector, count);
}

ssize_t 
cmm_write_with_deps(mc_socket_t sock, const void *buf, size_t len,
                    int numdeps, const irob_id_t *deps, 
                    u_long send_labels, resume_handler_t fn, void *arg,
                    irob_id_t *irob_id)
{
    return cmm_send_with_deps(sock, buf, len, 0, numdeps, deps, 
                              send_labels, fn, arg, irob_id);
}

ssize_t 
cmm_send_with_deps(mc_socket_t sock, const void *buf, size_t len, int flags,
                   int numdeps, const irob_id_t *deps, 
                   u_long send_labels, resume_handler_t fn, void *arg,
                   irob_id_t *irob_id)
{
    irob_id_t id = begin_irob(sock, numdeps, deps, send_labels, fn, arg);
    if (id < 0) {
        if (errno == EBADF) {
            // this must not be a multisocket.  send it regular-style!
            return cmm_send(sock, buf, len, flags, send_labels, fn, arg);
        } else {
            // some other error; just return it
            return id;
        }
    }

    ssize_t bytes_sent = irob_send(id, buf, len, flags);
    if (bytes_sent < 0) {
        return bytes_sent;
    }

    int rc = end_irob(id);
    if (rc < 0) {
        return rc;
    }

    if (irob_id) {
        *irob_id = id;
    }
    return bytes_sent;
}

int 
cmm_writev_with_deps(mc_socket_t sock, const struct iovec *vector, int count,
                     int numdeps, const irob_id_t *deps, 
                     u_long send_labels, resume_handler_t fn, void *arg,
                     irob_id_t *irob_id)
{
    irob_id_t id = begin_irob(sock, numdeps, deps, send_labels, fn, arg);
    if (id < 0) {
        if (errno == EBADF) {
            // this must not be a multisocket.  send it regular-style!
            return cmm_writev(sock, vector, count, send_labels, fn, arg);
        } else {
            // some other error; just return it
            return id;
        }
    }

    int bytes_sent = irob_writev(id, vector, count);
    if (bytes_sent < 0) {
        return bytes_sent;
    }

    int rc = end_irob(id);
    if (rc < 0) {
        return rc;
    }

    if (irob_id) {
        *irob_id = id;
    }
    return bytes_sent;
}

int 
irob_relabel(irob_id_t irob_id, u_long new_labels)
{
    return CMMSocket::lookup_by_irob(irob_id)->irob_relabel(irob_id, new_labels);
}


/* Private functions; only exported via private header. */

void
CMM_PRIVATE_drop_irob_and_dependents(irob_id_t irob)
{
    CMMSocketPtr sk(CMMSocket::lookup_by_irob(irob));
    if (sk) {
        CMMSocketImpl *skp = (CMMSocketImpl *) sk.get();
        skp->drop_irob_and_dependents(irob);
    }
}
