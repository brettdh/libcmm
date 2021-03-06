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

#ifndef BUILDING_EXTERNAL
#include <instruments.h>
#include <eval_method.h>
#endif

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

/* available labels */
//#define CMM_LABEL_RED 0x1
//#define CMM_LABEL_BLUE 0x2

// use at most one of these two.
#define CMM_LABEL_ONDEMAND 0x4
#define CMM_LABEL_BACKGROUND 0x8

// use at most one of these two.
#define CMM_LABEL_SMALL 0x10
#define CMM_LABEL_LARGE 0x20

#define CMM_LABEL_UNUSED 0x40
#define CMM_LABEL_ALL (CMM_LABEL_UNUSED - 1)
#define CMM_LABEL_COUNT 6

#define MAX_LABEL_LENGTH 20

enum IntNWNetworkStrategyType {
    INTNW_NEVER_REDUNDANT=0,
    ALWAYS_REDUNDANT,
    INTNW_REDUNDANT,
    CELLULAR_ONLY,
    WIFI_PREFERRED,
    
    NUM_REDUNDANCY_STRATEGY_TYPES
};

int get_redundancy_strategy_type(const char *strategy_name);

#define SO_CMM_REDUNDANCY_STRATEGY 0x4002

/*** CMM socket function wrappers ***/

/* For all functions:
 *  -arg must not point to data on the stack.
 *  -If arg is heap-allocated, resume_handler must save it (perhaps in
 *     another thunk) or free it before returning. (but see below EFFECT) 
 */
typedef void (*resume_handler_t)(void* arg);

/* EFFECT: sending operations may "succeed" in two different ways.
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

/* sending operations.
 *
 * send_labels specify desired characteristics of network connections.
 *
 * These functions create "anonymous" IROBs (see libcmm_irob.h) that depend
 *  upon all other IROBs currently in flight. Additionally, all IROBs 
 *  implicitly depend on the most recent anonymous IROB, if any are in flight. 
 *  Thus, a sequence of anonymous sends will be received in the order in which 
 *  they were sent.
 * As a result, applications that do not explicitly use IROBs will
 *  nonetheless be guaranteed an ordered bytestream.
 * See libcmm_irob.h on how to use IROBs and explicit dependencies for 
 *  potential performance gains by reordering.
 */
ssize_t cmm_send(mc_socket_t sock, const void *buf, size_t len, int flags,
                 u_long send_labels, 
                 resume_handler_t handler, void *arg);
ssize_t cmm_write(mc_socket_t sock, const void *buf, size_t len,
                 u_long send_labels, 
                 resume_handler_t handler, void *arg);
int cmm_writev(mc_socket_t sock, const struct iovec *vector, int count,
               u_long send_labels, 
               resume_handler_t handler, void *arg);

/* receiving operations.
 *
 * Receiving operations on multi-sockets record the labels that the
 *  remote sender specified for this operation.
 */
int cmm_read(mc_socket_t sock, void *buf, size_t count, u_long *recv_labels);
int cmm_recv(mc_socket_t sock, void *buf, size_t count, int flags,
             u_long *recv_labels);

/* simple, no-thunk wrappers */
int cmm_getsockopt(mc_socket_t sock, int level, int optname, 
                   void *optval, socklen_t *optlen);
int cmm_setsockopt(mc_socket_t sock, int level, int optname, 
                   const void *optval, socklen_t optlen);

/* fd_sets can contain mc_sockets and real os fds. */
int cmm_select(mc_socket_t nfds, 
               fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
               struct timeval *timeout);

int cmm_poll(struct pollfd fds[], nfds_t nfds, int timeout);

// cause all threads waiting for input on sock with cmm_select
// to be interrupted and return -1, errno=EINTR.
// XXX: this is a hack.  I think it would make more sense
// XXX: to implement a "filtered receive" with network-restriction
// XXX: labels that returns an appropriate error if the required
// XXX: network goes away.
void cmm_interrupt_waiters(mc_socket_t sock);

// XXX: broken.  Kind of silly, too, since if you called
//    cmm_connect, you know the answer already.
int cmm_getpeername(int socket, struct sockaddr *address, socklen_t *address_len);

// XXX: getsockname doesn't make a lot of sense for multisockets.
//   right now it's just a bit of fakery. Code that uses it
//   for anything more significant than debug  printfs is probably
//   going to break.
int cmm_getsockname(int socket, struct sockaddr *address, socklen_t *address_len);

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
 *   Even so, it's probably still somewhat useful to know at least
 *   the physical address of the connecting host.
 * For now, it will be filled in with the remote address of the 
 *   initial underlying physical connection, even though this seems to be
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

/* Looks through the socket's queue of thunks and cancels any and all
 * that match this handler.
 * If deleter is non-NULL, it will be called on the handler's arg. */
/* returns the number of thunks cancelled. */
int cmm_thunk_cancel(mc_socket_t sock, u_long label, 
                     void (*handler)(void*), void *arg,
                     void (*deleter)(void*));

/* functions for getting/setting the failure timeout. 
 * this timeout is invoked when calling a multi-socket operation
 * without a thunk.  If the multisocket is unable to send data
 * with the requested label, it will block until either a
 * suitable network becomes available or the timeout expires.
 * the defailt timeout is {-1, 0}, meaning that no-thunk
 * calls will block indefinitely. */
/* XXX: these are untested beyond the default. */
int cmm_get_failure_timeout(mc_socket_t sock, u_long label, struct timespec *ts);
int cmm_set_failure_timeout(mc_socket_t sock, u_long label, const struct timespec *ts);

typedef void *instruments_context_t;
struct intnw_network_strategy;
typedef struct intnw_network_strategy * intnw_network_strategy_t;

/* Retrieve the strategy that IntNW will currently use when selecting
 * a network or networks.  For a typical phone, this is "cellular", "wifi", or both.
 * This strategy can then be passed to cmm_estimate_transfer_time 
 * to compose a higher-level application's strategy; e.g.
 * remote-exec is local or remote or both, and remote/both is composed from
 * the IntNW-based transfer time/energy/data estimates.
 *
 * Note: this also grabs a lock, so that the strategy will not change
 *  while performing external calculations.  Caller must release this lock
 *  when finished by calling cmm_free_network_strategy.
 */
intnw_network_strategy_t
cmm_get_network_strategy(mc_socket_t sock, instruments_context_t context);

/* Same as above, but allows explicit restriction to one network or the other.
 */
intnw_network_strategy_t
cmm_get_network_strategy_with_restriction(mc_socket_t sock, instruments_context_t context,
                                          u_long net_restriction_labels);

#ifndef BUILDING_EXTERNAL
instruments_estimator_t
cmm_get_latency_estimator(mc_socket_t sock, u_long net_restriction_labels);
#endif

/* Frees the opaque handle to IntNW's current network strategy and releases its lock.
 */
void
cmm_free_network_strategy(mc_socket_t sock, intnw_network_strategy_t strategy);

/* Estimate the time (seconds) it will take to deliver datalen bytes on this socket.
 * This is time to receive the ACK for all the data, so it always includes
 * one full roundtrip.
 */
double cmm_estimate_transfer_time(mc_socket_t sock, 
                                  intnw_network_strategy_t strategy, 
                                  u_long labels, 
                                  size_t datalen);

/* same as above, but for energy and cellular data. */
double cmm_estimate_transfer_energy(mc_socket_t sock, 
                                    intnw_network_strategy_t strategy, 
                                    u_long labels, 
                                    size_t datalen);
double cmm_estimate_transfer_data(mc_socket_t sock, 
                                  intnw_network_strategy_t strategy, 
                                  u_long labels, 
                                  size_t datalen);

/* looks up the oldest unack'd IROB and returns the 
 * time since it was sent (in seconds). 
 * Returns 0.0 if there are no unack'd IROBs. */
double cmm_get_oldest_irob_delay(mc_socket_t sock);

#ifndef BUILDING_EXTERNAL
/* return the configured eval method. */
enum EvalMethod cmm_get_estimator_error_eval_method();
#endif

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
/* simple deleter function that calls the delete operator on its victim. 
 * only valid if we're compiling with a C++ compiler;
 * thunks with malloc()'d arguments can just pass 
 * stdlib's free as the deleter. */
template <typename T>
void delete_arg(void *victim)
{
    T *real_victim = (T*)victim;
    delete real_victim;
}

#endif

#endif
