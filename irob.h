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

/* Identifer for an IROB, assigned by begin_irob[_label] 
 * and released by end_irob 
 */
typedef int irob_id_t;

/* Begin an IROB and return the new IROB id.
 * If deps is non-NULL, it should point to a depnum-sized array of
 * irob_id_t's, indicating which IROBs this IROB depends on.
 * If deps is NULL, then this IROB has no dependencies. */
irob_id_t begin_irob(int sock, const irob_id_t *deps, int depnum);

/* Begin an IROB on a multi-socket.
 * Such IROBs have labels and a thunk associated, just as
 * traditional multi-socket send operations do. */
irob_id_t begin_irob_label(mc_socket_t sock, const irob_id_t *deps, int depnum,
                           u_long labels, resume_handler_t fn, void *arg);

/* End an IROB, notifying the receiver that it is complete. */
int end_irob(irob_id_t id);

/* Sending operations for IROBs.  Since an irob_id implies a socket,
 * the socket argument is not required. 
 * Recall that send operations within an IROB are guaranteed to be 
 * received in their original order (see receiver API below). */
ssize_t irob_send(irob_id_t irob_id, const void *buf, size_t len, int flags);
int irob_writev(irob_id_t irob_id, const struct iovec *vector, int count);
/* ...etc. */

/* These functions allow a single send operation inside a so-called
 * "anonymous" IROB that depends on all previous IROBs that have not
 * yet been received at the receiver application. 
 *
 * (This is the "default-safe" send that we talked about.) */
ssize_t anon_irob_send(int sock, const void *buf, size_t len, int flags);
int anon_irob_writev(int sock, const struct iovec *vector, int count);
/* ...etc. */

/* Anonymous IROB sending operations on multi-sockets. */
ssize_t 
anon_irob_mc_send(mc_socket_t sock, const void *buf, size_t len, int flags,
                  u_long labels, resume_handler_t fn, void *arg);
int anon_irob_mc_writev(mc_socket_t sock, const struct iovec *vector, int count,
                        u_long labels, resume_handler_t fn, void *arg);
/* ...etc. */


/* and now for the receiver API */

/* Receive an IROB's data bytes on the socket.  Bytes that are read
 * from a socket using these functions are guaranteed to be in an
 * order that respects the constraints specified by the sender's
 * IROBs. */
ssize_t irob_recv(int sock, void *buf, size_t len, int flags);
ssize_t irob_read(int sock, void *buf, size_t len);
/* ... */

/* Receive an IROB's data bytes on a multi-socket.  If plabels is
 * non-NULL, the sender's labels will be stored at *plabels. */
ssize_t irob_mc_recv(mc_socket_t sock, void *buf, size_t len, int flags,
                     u_long *plabels /* OUT */);
ssize_t irob_mc_read(mc_socket_t sock, void *buf, size_t len,
                     u_long *plabels /* OUT */);
/* ... */

