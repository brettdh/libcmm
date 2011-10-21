#include <stdio.h>
#include <signal.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <map>
#include <vector>
#include <memory>
//#include <stropts.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <fcntl.h>
#include <fstream>
#include <string>
using std::map;
using std::ifstream; using std::string;
using std::vector; using std::pair;
using std::auto_ptr;

#include "libcmm.h"
#include "libcmm_ipc.h"
#include "libcmm_net_restriction.h"
#include "pending_irob.h"
#include "debug.h"

#include "cmm_timing.h"
#include "pthread_util.h"

#include "cmm_socket.h"
#include "thunks.h"
#include "cmm_thread.h"

#define CONFIG_FILE "/etc/cmm_config"

static void libcmm_init(void) __attribute__((constructor));
static void libcmm_init(void)
{
#ifdef CMM_DEBUG
    set_debugging(false); // default: no dbgprintfs
#endif

    ifstream config_input(CONFIG_FILE);
    if (config_input) {
        string line;
        while (getline(config_input, line)) {
#ifdef CMM_DEBUG
            size_t pos = line.find("debug");
            if (pos != string::npos) {
                set_debugging(true);
            }
#endif
        }
        config_input.close();
    } else {
        dbgprintf_always("Warning: config file not read; couldn't open %s\n",
                CONFIG_FILE);
    }
    
    if (scout_ipc_init() < 0) {
        // XXX: is there any problem with doing this here?
        exit(EXIT_FAILURE);
    }

#ifdef CMM_TIMING
    PthreadScopedLock lock(&timing_mutex);
    struct timeval now;
    TIME(now);
    timing_file = fopen(TIMING_FILE, "a");
    if (timing_file) {
        fprintf(timing_file, "*** Started new run at %ld.%06ld, PID %d\n",
                now.tv_sec, now.tv_usec, getpid());
    }
#endif
}


static void libcmm_deinit(void) __attribute__((destructor));
static void libcmm_deinit(void)
{
#ifdef CMM_TIMING
    {
//         printf("Exiting; %d PendingIROBs still exist\n",
//                PendingIROB::objs());

        PthreadScopedLock lock(&timing_mutex);
        
        if (timing_file) {
            struct timeval now;
            TIME(now);
            fprintf(timing_file, "*** Finished run at %ld.%06ld, PID %d;\n",
                    now.tv_sec, now.tv_usec, getpid());

            //fclose(timing_file);
        }
    }
#endif

#if 0
    ThunkHash::iterator it;

    for (it = thunk_hash.begin(); it != thunk_hash.end(); it++) {
        struct labeled_thunk_queue *tq = it->second;
        delete tq;
    }
    thunk_hash.clear();
#endif

    scout_ipc_deinit();

    //CMMThread::join_all();
    dbgprintf("Main thread exiting.\n");
    //pthread_exit(NULL);
}

/* Figure out how the network status changed and invoke all the 
 * queued handlers that can now be processed. */
void process_interface_update(struct net_interface iface, bool down)
{
    /* 1) Read a message from the queue to determine what labels
     *    are available.
     * 2) For each available label, look through the queues for thunks
     *    with matching labels and execute the handlers, removing the thunks
     *    from the queues.  
     *    NOTE: we need to make sure this matching strategy
     *      is the same one employed by the kernel.  That's not really ideal.
     *    EDIT: well, sorta.  The kernel will eventually have to tell us
     *      what application-level labels an interface matches, even though
     *      that may change over time.
     * 3) Clean up.
     */

    //dbgprintf_always("Signalled by scout\n");
    
    /* bitmask of all available bit labels ORed together */
    dbgprintf("Got update from scout: %s is %s, bandwidth_down %lu bandwidth_up %lu bytes/sec "
              "RTT %lu ms type %s\n",
              inet_ntoa(iface.ip_addr), down?"down":"up",
              iface.bandwidth_down, iface.bandwidth_up, iface.RTT,
              net_type_name(iface.type));

    //dbgprintf_always("Before:\n---\n");
    //print_thunks();

    if (down) {
        /* put down the sockets connected on now-unavailable networks. */
        CMMSocket::interface_down(iface);
    } else {    
        /* fire thunks thunk'd on now-available network. */
        CMMSocket::interface_up(iface);
        //fire_thunks();
    }

    //dbgprintf_always("After:\n---\n");
    //print_thunks();
}


/*** CMM socket function wrappers ***/

ssize_t cmm_send(mc_socket_t sock, const void *buf, size_t len, int flags,
                 u_long send_labels, 
                 void (*resume_handler)(void*), void *arg)
{
    return CMMSocket::lookup(sock)->mc_send(buf, len, flags,
                                            send_labels, 
                                            resume_handler, arg);
}

ssize_t cmm_write(mc_socket_t sock, const void *buf, size_t len,
                  u_long send_labels, 
                  void (*resume_handler)(void*), void *arg)
{
    return cmm_send(sock, buf, len, 0, send_labels, resume_handler, arg);
}

int cmm_writev(mc_socket_t sock, const struct iovec *vec, int count,
               u_long send_labels, 
               void (*resume_handler)(void*), void *arg)
{
    return CMMSocket::lookup(sock)->mc_writev(vec, count,
                                              send_labels, 
                                              resume_handler, arg);
}

int cmm_select(mc_socket_t nfds, 
               fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
               struct timeval *timeout)
{
    return CMMSocket::mc_select(nfds, readfds, writefds, exceptfds, timeout);

#if 0
    /* No longer needed, since we now select on a special
     * file descriptor that captures all CSockets */
    int rc = 0;
    do {
        rc = CMMSocket::mc_select(nfds, readfds, writefds, exceptfds, timeout);
        if (rc < 0 && errno == EINTR) {
            dbgprintf("Select interrupted by signal; retrying "
                    "(inside libcmm)\n");
        } else {
            dbgprintf("mc_select returned %d, errno=%d\n", rc, errno);
        }
    } while (rc < 0 && errno == EINTR);
    return rc;
#endif
}

int cmm_poll(struct pollfd fds[], nfds_t nfds, int timeout)
{
    return CMMSocket::mc_poll(fds, nfds, timeout);
}

int cmm_getpeername(mc_socket_t sock, struct sockaddr *address, 
                    socklen_t *address_len)
{
    return CMMSocket::lookup(sock)->mc_getpeername(address, address_len);
}

int cmm_getsockname(mc_socket_t sock, struct sockaddr *address, 
                    socklen_t *address_len)
{
    return CMMSocket::lookup(sock)->mc_getsockname(address, address_len);
}

int cmm_listen(int listener_sock, int backlog)
{
    return CMMSocket::mc_listen(listener_sock, backlog);
}

mc_socket_t cmm_accept(int listener_sock, 
                       struct sockaddr *addr, socklen_t *addrlen)
{
    return CMMSocket::mc_accept(listener_sock, addr, addrlen);
}

int cmm_read(mc_socket_t sock, void *buf, size_t count, u_long *recv_labels)
{
    return cmm_recv(sock, buf, count, 0, recv_labels);
}

int cmm_recv(mc_socket_t sock, void *buf, size_t count, int flags,
             u_long *recv_labels)
{
    return CMMSocket::lookup(sock)->mc_recv(buf, count, flags, recv_labels);
}

int cmm_getsockopt(mc_socket_t sock, int level, int optname, 
                   void *optval, socklen_t *optlen)
{
    return CMMSocket::lookup(sock)->mc_getsockopt(level, optname, 
                                                  optval, optlen);
}

int cmm_setsockopt(mc_socket_t sock, int level, int optname, 
                   const void *optval, socklen_t optlen)
{
    return CMMSocket::lookup(sock)->mc_setsockopt(level, optname,
                                                  optval, optlen);
}

int cmm_connect(mc_socket_t sock, 
                const struct sockaddr *serv_addr, socklen_t addrlen)
{
    return CMMSocket::lookup(sock)->mc_connect(serv_addr, addrlen);
}

mc_socket_t cmm_socket(int family, int type, int protocol)
{
    return CMMSocket::create(family, type, protocol);
}

int cmm_shutdown(mc_socket_t sock, int how)
{
    return CMMSocket::lookup(sock)->mc_shutdown(how);
}

int cmm_close(mc_socket_t sock)
{
    return CMMSocket::mc_close(sock);
}

/* if deleter is non-NULL, it will be called on the handler's arg. */
int cmm_thunk_cancel(mc_socket_t sock, u_long send_labels,
                     void (*handler)(void*), void *arg,
                     void (*deleter)(void*))
{
    return cancel_thunk(sock, send_labels, handler, arg, deleter);
}

int cmm_get_failure_timeout(mc_socket_t sock, u_long label, struct timespec *ts)
{
    return CMMSocket::lookup(sock)->mc_get_failure_timeout(label, ts);
}

int cmm_set_failure_timeout(mc_socket_t sock, u_long label, const struct timespec *ts)
{
    return CMMSocket::lookup(sock)->mc_set_failure_timeout(label, ts);
}
