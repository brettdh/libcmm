#ifndef libcmm_constraints_incl
#define libcmm_constraints_incl

#include <libcmm.h>
#include <sys/uio.h>

typedef unsigned long substream_id_t;

substream_id_t begin_substream(mc_socket_t sock, 
                               int depnum, substream_set_t *dependencies);

/* End an ordered substsream, firing its operations in order.
 * Returns CMM_SUCCESS if all operations completed successfully.
 * Returns (respectively CMM_FAILED/CMM_DEFERRED if any operation 
 *  fails or is deferred, stopping after the first failed/deferred 
 *  operation.  (The effect of this behavior is that at most one
 *  thunk will be registered by a call to end_substream.)
 */
int end_substream(substream_id_t substream);


/* Enqueueable operations on an ordered substream, below. 
 * They differ from their respective syscalls in that they 
 * return 0 on success, rather than the number of bytes sent.
 * This is because no bytes are actually sent until end_substream 
 * is called. */

ssize_t substream_send(substream_id_t substream, 
                       const void *buf, size_t len, int flags,
                       u_long labels, resume_handler_t handler, void *arg);
int substream_writev(substream_id_t substream, 
                     const struct iovec *vector, int count,
                     u_long labels, resume_handler_t handler, void *arg);

#endif
