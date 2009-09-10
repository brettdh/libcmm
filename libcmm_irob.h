#ifndef LIBCMM_IROB_H_INCL
#define LIBCMM_IROB_H_INCL

/* API sketch for IROBs - Isolated Reliable Ordered Bytestreams 
 *
 * The IROB abstraction provides the following guarantees:
 *  1) Isolation: no send operations from other IROBs can interleave with the 
 *     send operations from this IROB. (AKA mutual exclusion)
 *     Note that this does not imply atomicity; a partial IROB may be
 *     received if connectivity to the receiver is interrupted.
 *  2) Reliability: TCP-like reliable delivery on each send operation
 *     inside an IROB.
 *  3) Ordering: (a) Send operations within an IROB are received in the
 *     order in which they were sent; and (b) all IROBs are
 *     received in an order that respects the declared *dependencies* 
 *     between them (see below).  This is a weakening of TCP's
 *     total-order guarantee for a single stream, in order to allow
 *     some amount of reordering for performance.
 */

/* NOTE: though it does make sense to consider IROBs in the absence
 * of multi-sockets, we are not currently implementing this.
 * Functions below that would make up a non-multisocket IROB API have
 * thus been commented out, with a NOTE explaining why.  The comments 
 * on the non-multisocket IROB functions do apply to their multisocket 
 * counterparts.
 *
 * Similarly, the safe-by-default "anonymous" IROB functions have 
 * been made the default operations on multi-sockets, so they are no
 * longer needed as separate functions.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>
#include <libcmm.h>

/* Identifer for an IROB, assigned by begin_irob[_label] 
 * and released by end_irob 
 */
typedef long irob_id_t;

/* Begin an IROB and return the new IROB id.
 * If deps is non-NULL, it should point to a depnum-sized array of
 * irob_id_t's, indicating which IROBs this IROB depends on.
 * If deps is NULL, then this IROB has no dependencies. */
//irob_id_t begin_irob(int sock, const irob_id_t *deps, int depnum);
// NOTE: not implemented; non-multi-socket

/* Begin an IROB on a multi-socket.  
 * Such IROBs have sender labels, receiver labels, and a thunk
 * associated, just as traditional multi-socket send operations do. */
irob_id_t begin_irob(mc_socket_t sock, int numdeps, const irob_id_t *deps, 
                     u_long send_labels,
                     resume_handler_t fn, void *arg);

/* End an IROB, notifying the receiver that it is complete. */
int end_irob(irob_id_t id);

/* Sending operations for IROBs.  Since an irob_id implies a socket,
 * the socket argument is not required. 
 * Recall that send operations within an IROB are guaranteed to be 
 * received in their original order. */
ssize_t irob_send(irob_id_t irob_id, const void *buf, size_t len, int flags);
int irob_writev(irob_id_t irob_id, const struct iovec *vector, int count);
/* ...etc. */

/* ---------------------------------------------------------------- * 
 * NOTE: everything below this point is comments and unimplemented  *
 * functions, as per the NOTE at the top of this header.            *
 * ---------------------------------------------------------------- */

/* These functions allow a single send operation inside a so-called
 * "anonymous" IROB that depends on all previous IROBs that have not
 * yet been received at the receiver application. 
 *
 * (This is the "default-safe" send that we talked about.) */
// ssize_t anon_irob_send(int sock, const void *buf, size_t len, int flags);
// int anon_irob_writev(int sock, const struct iovec *vector, int count);
/* ...etc. */

/* Anonymous IROB sending operations on multi-sockets. */
/* 
ssize_t 
anon_irob_mc_send(mc_socket_t sock, const void *buf, size_t len, int flags,
                  u_long labels, resume_handler_t fn, void *arg);
int anon_irob_mc_writev(mc_socket_t sock, const struct iovec *vector, int count,
                        u_long labels, resume_handler_t fn, void *arg);
*/
/* ...etc. */
/* NOTE: these are simply going to be the default cmm_send and cmm_writev;
 * those functions are being replaced with functions that do the 
 * safe-by-default anonymous IROB stuff described above.  Thus, it is
 * easier to use the defaults safely than to misuse the irob_* functions
 * and get unsafe reordering.
 */


/* and now for the receiver API */

/* Receive an IROB's data bytes on the socket.  Bytes that are read
 * from a socket using these functions are guaranteed to be in an
 * order that respects the constraints specified by the sender's
 * IROBs. */
//ssize_t irob_recv(int sock, void *buf, size_t len, int flags);
//ssize_t irob_read(int sock, void *buf, size_t len);
// NOTE: not implemented; non-multi-socket
/* ... */

/* Receive an IROB's data bytes on a multi-socket.  If plabels is
 * non-NULL, the sender's labels will be stored at *plabels. */
/*
ssize_t irob_mc_recv(mc_socket_t sock, void *buf, size_t len, int flags,
                     u_long *recv_labels);
ssize_t irob_mc_read(mc_socket_t sock, void *buf, size_t len,
                     u_long *recv_labels);
*/
/* ... */
/* NOTE: since everything sent on a multi-socket is now an IROB, these
 * are the default mc_recv and mc_read operations.
 */

#ifdef __cplusplus
}
#endif

#endif
