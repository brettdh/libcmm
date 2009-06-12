#ifndef LIBCMM_H_INCL
#define LIBCMM_H_INCL

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
#define CMM_SELECT_SIGNAL 42 /* I am assured this is okay in Linux. */

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

/* fd_sets can contain mc_sockets and real os fds. */
int cmm_select(mc_socket_t nfds, 
	       fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
	       struct timeval *timeout);

int cmm_read(mc_socket_t sock, void *buf, size_t count);
int cmm_poll(struct pollfd fds[], nfds_t nfds, int timeout);
int cmm_getpeername(int socket, struct sockaddr *address, socklen_t *address_len);

/* devnote: conn_[down|up]_cbs are no longer necessary when we own
 * a piece at both ends of the connection (which we will; see also
 * cmm_listen, cmm_accept). */
int cmm_connect(mc_socket_t sock, 
                const struct sockaddr *serv_addr, socklen_t addrlen);

/* use these in place of listen/accept to receive multi-socket connections.
 * listener_sock should itself not be a multi-socket. */

int cmm_listen(int listener_sock, int backlog);

/* cmm_accept creates and returns a multi-soocket.
 * listener_sock should have been first passed to cmm_listen;
 *   cmm_accept will return an error otherwise.
 * XXX: perhaps the addr/addrlen params should be removed, since
 *   there's no way to describe the logical address at the other end.
 * For now, it will be filled in with the remote address of the 
 *   underlying physical connection, even though this seems to be
 *   poking a hole through the abstraction. */
mc_socket_t cmm_accept(int listener_sock, 
                       struct sockaddr *addr, socklen_t *addrlen);

/* applications should use these in place of socket() and close()
 * to create and destroy mc_sockets. */

/* returns a usable mc_socket_t on success, -1 on failure. 
 */
mc_socket_t cmm_socket(int family, int type, int protocol);

/* returns 0 on success, -1 on failure. */
int cmm_close(mc_socket_t sock);

/* returns 0 on success, -1 on failure. */
int cmm_shutdown(mc_socket_t sock, int how);

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
/* returns the number of thunks cancelled. */
int cmm_thunk_cancel(u_long label, 
		     void (*handler)(void*), void *arg,
		     void (*deleter)(void*));


#ifdef __cplusplus
}
#endif

#endif
