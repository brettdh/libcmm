#ifndef LIBCMM_INCL_H
#define LIBCMM_INCL_H

/* libcmm.h: Connection Manager Manager
 * 
 * Provides wrapper functions for standard socket calls,
 * including support for attaching qualitative labels
 * to socket operations, optionally attaching a resume handler
 * function and argument, to be invoked when a network interface
 * with the desired characteristics becomes available.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/uio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/poll.h>

/* opaque socket structure; multi-colored socket */
typedef int mc_socket_t;

/* return codes for cmm_*() functions; see below */
#define CMM_FAILED -1
#define CMM_DEFERRED -2

/* signal, fired from connection scout, used to invoke thunks */
#define CMM_SIGNAL SIGVTALRM

/*** CMM socket function wrappers ***/

/* For all functions:
 *  -arg must not point to data on the stack.
 *  -If arg is heap-allocated, resume_handler must save it (perhaps in
 *     another thunk) or free it before returning. (but see below EFFECT) 
 */

/* REQUIREMENT: resume_handler CANNOT call any cmm_* functions with 
 * different labels than it was thunk'd with. */
typedef void (*resume_handler_t)(void* arg);

/* EFFECT: cmm_ operations may "succeed" in two different ways.
 *  1) The network is available, and the send can go out immediately,
 *     so it proceeds and returns with the semantics of the underlying syscall.
 *  2) The network is not available, so the thunk is enqueued for
 *     execution later.  This result returns CMM_DEFERRED.
 *     
 *  Since the thunk is required to free or save its argument, the caller 
 *  should treat the pointer it passed as dangling in case 2.
 *  In case 1, the thunk was not called, so the caller should free
 *  or save its argument.
 */

/* adding wrappers as needed, no sooner */
ssize_t cmm_send(mc_socket_t sock, const void *buf, size_t len, int flags,
		 u_long labels, resume_handler_t handler, void *arg);
int cmm_writev(mc_socket_t sock, const struct iovec *vector, int count,
	       u_long labels, resume_handler_t handler, void *arg);

/* simple, no-thunk wrappers */
int cmm_getsockopt(mc_socket_t sock, int level, int optname, 
		   void *optval, socklen_t *optlen);
int cmm_setsockopt(mc_socket_t sock, int level, int optname, 
		   const void *optval, socklen_t optlen);

/* first three fd_sets may only contain real os file descriptors.
 * next three fd_sets may only contain mc_socket_t's. */
/* XXX: this can be greatly simplified, since we now reserve real os 
 * file descriptors when calling cmm_socket.  Basically, we don't need
 * separate fd sets; we can just look up all the fds in the hash
 * and figure out which ones are really mc_sockets. */
int cmm_select(mc_socket_t nfds, 
	       fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
	       fd_set *mc_readfds, fd_set *mc_writefds, fd_set *mc_exceptfds,
	       struct timeval *timeout);

int cmm_read(mc_socket_t sock, void *buf, size_t count);
int cmm_poll(struct pollfd fds[], nfds_t nfds, int timeout);
int cmm_getpeername(int socket, struct sockaddr *address, socklen_t *address_len);

/* connection callbacks take as arguments the socket file descriptor
 * in question, the labels for this reconnection, and a pointer to 
 * any other needed data, provided with cmm_connect. 
 * REQUIREMENT: these must only call cmm_ functions with 
 * the same labels that they are passed. */
typedef int (*connection_event_cb_t)(mc_socket_t sock, u_long labels, 
				     void* arg);

/* cmm_connect stores the two socket event callbacks and a shared argument.
 * label_down_cb is called any time the connection needs to be torn down, and
 * label_up_cb is called any time the connection needs to be re-established.
 *
 * Requirements:
 *   -label_down_cb should not cmm_close() the socket.
 *   -label_up_cb should assume that the mc_socket is connected. After the
 *     initial call to cmm_connect, the library takes care of setting up 
 *     connections as needed before sending data.
 */
int cmm_connect(mc_socket_t sock, 
		const struct sockaddr *serv_addr, socklen_t addrlen, 
		u_long initial_labels,
		connection_event_cb_t label_down_cb,
		connection_event_cb_t label_up_cb,
		void *cb_arg);


/* applications should use these in place of socket() and close()
 * to create and destroy mc_sockets. */

/* returns a usable mc_socket_t on success, -1 on failure. */
mc_socket_t cmm_socket(int family, int type, int protocol);

/* returns 0 on success, -1 on failure. */
int cmm_shutdown(mc_socket_t sock, int how);

/* returns 0 on success, -1 on failure. */
int cmm_close(mc_socket_t sock);

/* checks whether label is available.
 * if so, prepares socket for sending on that label and returns 0.
 * if not, tries to register (fn,arg)  */
int cmm_check_label(mc_socket_t sock, u_long label,
		    resume_handler_t fn, void *arg);

/* to be called on a mc_socket that has encountered some kind of error.
 * "Resets" the mc_socket to the state before any real connections
 * have been made and initialized. The next operation on the 
 * mc_socket after cmm_reset will trigger reconnection and 
 * replay of the application-level callback. */
int cmm_reset(mc_socket_t sock);
  
/* This one is not tested yet. */
/* Looks through the socket's queue of thunks and cancels any and all
 * that match this handler.
 * If deleter is non-NULL, it will be called on the handler's arg. */
void cmm_thunk_cancel(mc_socket_t sock, 
		      void (*handler)(void*), void (*deleter)(void*));


#ifdef __cplusplus
}
#endif

#endif /* LIBCMM_INCL_H */
