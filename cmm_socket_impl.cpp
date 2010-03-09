#include "cmm_socket.h"
#include "cmm_socket.private.h"
#include "cmm_internal_listener.h"
#include "cmm_conn_bootstrapper.h"
#include "csocket.h"
#include "csocket_mapping.h"
#include "common.h"
#include "net_interface.h"
#include "libcmm.h"
#include "libcmm_ipc.h"
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

#include <memory>
using std::auto_ptr;

#include <signal.h>
#include "signals.h"
#include "thunks.h"
#include "cmm_timing.h"

#include "pending_irob.h"
#include "pending_sender_irob.h"

#include "debug.h"

#include <map>
#include <vector>
#include <set>
#include <utility>
using std::multimap; using std::make_pair;
using std::map; using std::vector;
using std::set; using std::pair;
using std::max; using std::min;

CMMSockHash CMMSocketImpl::cmm_sock_hash;
VanillaListenerSet CMMSocketImpl::cmm_listeners;
NetInterfaceSet CMMSocketImpl::ifaces;
IROBSockHash CMMSocketImpl::irob_sock_hash;
irob_id_t CMMSocketImpl::g_next_irob;
pthread_mutex_t CMMSocketImpl::hashmaps_mutex = PTHREAD_MUTEX_INITIALIZER;

// struct timeval CMMSocketImpl::last_fg;
// struct timeval CMMSocketImpl::total_inter_fg_time;
// size_t CMMSocketImpl::fg_count;

// return true if the listener received is the zero-sentinel
//  marking the end of the list
bool CMMSocketImpl::recv_remote_listener(int bootstrap_sock)
{
    struct CMMSocketControlHdr hdr;
    int rc = recv(bootstrap_sock, &hdr, sizeof(hdr), 0);
    if (rc != sizeof(hdr)) {
	perror("recv");
	dbgprintf("Error receiving remote listener\n");
        throw -1;
    }
    short type = ntohs(hdr.type);
    if (type != CMM_CONTROL_MSG_NEW_INTERFACE) {
	dbgprintf("Expected NEW_INTERFACE msg, got %d\n", type);
        throw -1;
    }
    struct net_interface new_listener;
    memset(&new_listener.ip_addr, 0, sizeof(new_listener.ip_addr));
    new_listener.ip_addr = hdr.op.new_interface.ip_addr;
    new_listener.labels = ntohl(hdr.op.new_interface.labels);
    new_listener.bandwidth_down = ntohl(hdr.op.new_interface.bandwidth_down);
    new_listener.bandwidth_up = ntohl(hdr.op.new_interface.bandwidth_up);
    new_listener.RTT = ntohl(hdr.op.new_interface.RTT);
    
    if (new_listener.ip_addr.s_addr == 0) {
        // no more remote interfaces
        dbgprintf("Done receiving remote interfaces\n");
        return true;
    } else {
        PthreadScopedRWLock sock_lock(&my_lock, true);
        remote_ifaces.erase(new_listener); // make sure the values update
        remote_ifaces.insert(new_listener);
    }
    dbgprintf("Got new remote interface %s with labels %lu, "
              "bandwidth_down %lu bytes/sec bandwidth_up %lu bytes/sec RTT %lu ms\n",
	      inet_ntoa(new_listener.ip_addr), new_listener.labels,
              new_listener.bandwidth_down, new_listener.bandwidth_up, new_listener.RTT);
    return false;
}

// returns the number of remote ifaces.
int CMMSocketImpl::recv_hello(int bootstrap_sock)
{
    struct CMMSocketControlHdr hdr;
    int rc = recv(bootstrap_sock, &hdr, sizeof(hdr), 0);
    if (rc != sizeof(hdr) || ntohs(hdr.type) != CMM_CONTROL_MSG_HELLO) {
	perror("recv");
	dbgprintf("Error receiving hello\n");
        throw -1;
    }

    {
        PthreadScopedRWLock sock_lock(&my_lock, true);
        remote_listener_port = hdr.op.hello.listen_port;
    }
    return ntohl(hdr.op.hello.num_ifaces);
}

void CMMSocketImpl::recv_remote_listeners(int bootstrap_sock, int num_ifaces)
{
    //for (int i = 0; i < num_ifaces; i++) {
    bool done = false;
    while (!done) {
        done = recv_remote_listener(bootstrap_sock);
    }
}

void
CMMSocketImpl::send_hello(int bootstrap_sock)
{
    struct CMMSocketControlHdr hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.type = htons(CMM_CONTROL_MSG_HELLO);

    PthreadScopedRWLock sock_lock(&my_lock, false);
    assert(listener_thread);
    hdr.op.hello.listen_port = listener_thread->port();
    hdr.op.hello.num_ifaces = htonl(local_ifaces.size());
    
    int rc = send(bootstrap_sock, &hdr, sizeof(hdr), 0);
    if (rc != sizeof(hdr)) {
        perror("send");
        dbgprintf("Error sending hello\n");
        throw -1;
    }
}

void CMMSocketImpl::send_local_listener(int bootstrap_sock, 
                                        struct net_interface iface)
{
    struct CMMSocketControlHdr hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.type = htons(CMM_CONTROL_MSG_NEW_INTERFACE);
    hdr.op.new_interface.ip_addr = iface.ip_addr;
    hdr.op.new_interface.labels = htonl(iface.labels);
    hdr.op.new_interface.bandwidth_down = htonl(iface.bandwidth_down);
    hdr.op.new_interface.bandwidth_up = htonl(iface.bandwidth_up);
    hdr.op.new_interface.RTT = htonl(iface.RTT);
    dbgprintf("Sending local interface info: %s with labels %lu\n",
	      inet_ntoa(iface.ip_addr), iface.labels);
    int rc = send(bootstrap_sock, &hdr, sizeof(hdr), 0);
    if (rc != sizeof(hdr)) {
	perror("send");
	dbgprintf("Error sending local listener\n");
        throw -1;
    }
}

void CMMSocketImpl::send_local_listeners(int bootstrap_sock)
{
    PthreadScopedRWLock sock_lock(&my_lock, false);
    assert(listener_thread);
    
    for (NetInterfaceSet::iterator it = local_ifaces.begin();
         it != local_ifaces.end(); it++) {
        send_local_listener(bootstrap_sock, *it);
    }

    struct net_interface sentinel;
    memset(&sentinel, 0, sizeof(sentinel));
    send_local_listener(bootstrap_sock, sentinel);
}

void
CMMSocketImpl::startup_csocks()
{
    PthreadScopedRWLock sock_lock(&my_lock, true);
    for (NetInterfaceSet::iterator it = remote_ifaces.begin();
         it != remote_ifaces.end(); it++) {
        csock_map->setup(*it, false, false, false);
    }
    for (NetInterfaceSet::iterator it = local_ifaces.begin();
         it != local_ifaces.end(); it++) {
        csock_map->setup(*it, true, true, false);
    }
}

void 
CMMSocketImpl::add_connection(int sock, 
                              struct in_addr local_addr,
                              struct net_interface remote_iface)
{
    csock_map->add_connection(sock, local_addr, remote_iface);
}



int
CMMSocketImpl::connection_bootstrap(const struct sockaddr *remote_addr, 
                                    socklen_t addrlen, int bootstrap_sock)
{
    accepting_side = (bootstrap_sock != -1);

    try {
        int rc;
        {
            PthreadScopedLock lock(&scheduling_state_lock);
            listener_thread = new ListenerThread(this);
            rc = listener_thread->start();
            if (rc != 0) {
                delete listener_thread;
                throw rc;
            }
        }
        
        {
            PthreadScopedLock scoped_lock(&hashmaps_mutex);
            PthreadScopedRWLock sock_lock(&my_lock, true);
            struct sockaddr_in *ip_sockaddr = 
                (struct sockaddr_in *)remote_addr;
            if (ip_sockaddr->sin_addr.s_addr == htonl(INADDR_LOOPBACK)) {
                struct net_interface localhost;
                localhost.ip_addr.s_addr = htonl(INADDR_LOOPBACK);
                localhost.labels = 0;
                localhost.bandwidth_down = 100000000;
                localhost.bandwidth_up = 100000000;
                localhost.RTT = 0;

                local_ifaces.insert(localhost);
                remote_ifaces.insert(localhost);
            } else {
                local_ifaces = ifaces;
                if (bootstrap_sock == -1) {
                    // connect()-side only
                    struct net_interface bootstrap_iface;
                    memcpy(&bootstrap_iface.ip_addr, &ip_sockaddr->sin_addr,
                           sizeof(bootstrap_iface.ip_addr));
                    // arbitrary; will be overwritten
                    bootstrap_iface.bandwidth_down = 1250000;
                    bootstrap_iface.bandwidth_up = 1250000;
                    bootstrap_iface.RTT = 0;
                    bootstrap_iface.labels = 0;
                    remote_ifaces.insert(bootstrap_iface);
                }
            }
        }
        
        bootstrapper = new ConnBootstrapper(this, bootstrap_sock, 
                                            remote_addr, addrlen);
        bootstrapper->start();
        if (!is_non_blocking()) {
            bootstrapper->join();
            if (!bootstrapper->succeeded()) {
                dbgprintf("Bootstrap failed: %s\n",
                          strerror(bootstrapper->status()));
                //delete bootstrapper;
                //bootstrapper = NULL;
                throw -1;
            }
        } else {
            // EINPROGRESS an error
            errno = bootstrapper->status();
        }
    } catch (int error_rc) {
        PthreadScopedLock lock(&scheduling_state_lock);
        if (listener_thread) {
            listener_thread->stop();
        }
        return error_rc;
    }

    return 0;
}

// only called on bootstrapping.
void
CMMSocketImpl::wait_for_connections()
{
    csock_map->wait_for_connections();
}

int
CMMSocketImpl::connect_status()
{
    PthreadScopedRWLock sock_lock(&my_lock, false);
    assert(bootstrapper);
    return bootstrapper->status();
}

mc_socket_t
CMMSocketImpl::create(int family, int type, int protocol)
{
    if (family != PF_INET) {
        // TODO: remove this restriction and support 
        // more socket types.
        dbgprintf("Warning: only AF_INET supported.  cmm_socket returns "
                  "pass-through calls for AF %d.\n", family);
        return socket(family, type, protocol);
    }

    CMMSocketImplPtr new_sk;
    mc_socket_t new_sock = -1;
    try {
	/* automatically clean up if cmm_sock() throws */
        CMMSocketImplPtr tmp(new CMMSocketImpl(family, type, protocol));
	new_sock = tmp->sock;
        new_sk = tmp;
        new_sk->csock_map = new CSockMapping(new_sk);
    } catch (int oserr) {
	return oserr;
    }
    
    PthreadScopedLock lock(&hashmaps_mutex);
    if (!cmm_sock_hash.insert(new_sock, new_sk)) {
	fprintf(stderr, "Error: new socket %d is already in hash!  WTF?\n", 
		new_sock);
	assert(0);
    }

    return new_sock;
}

/* check that this file descriptor really refers to a multi-socket.
 * Qualifications:
 *   1) Must be a socket, and
 *   2) Must have our magic-number label set.
 */
static int
sanity_check(mc_socket_t sock)
{
    return 0;
    /* useless without Juggler. */
#if 0
    struct stat st;
    int rc = fstat(sock, &st);
    if (rc == 0) {
        u_long labels = 0;
        socklen_t len = sizeof(labels);
        if (st.st_mode & S_IFSOCK) {
            /* XXX: This won't work when we yank out Juggler. */
            rc = getsockopt(sock, SOL_SOCKET, SO_CONNMGR_LABELS,
                            &labels, &len);
            if (rc == 0 && labels == FAKE_SOCKET_MAGIC_LABELS) {
                return 0;
            }
        }
    }
    fprintf(stderr, "ERROR: mc_socket sanity check FAILED!\n");
    return -1;
#endif
}

CMMSocketPtr
CMMSocketImpl::lookup(mc_socket_t sock)
{
    CMMSocketImplPtr sk;
    if (!cmm_sock_hash.find(sock, sk)) {
        return CMMSocketPtr(new CMMSocketPassThrough(sock));
    } else {
        int rc = sanity_check(sock);
        assert(rc == 0);
        return sk;
    }
}

CMMSocketPtr
CMMSocketImpl::lookup_by_irob(irob_id_t id)
{
    IROBSockHash::const_accessor read_ac;
    if (!irob_sock_hash.find(read_ac, id)) {
        return CMMSocketPtr(new CMMSocketPassThrough(-1));
    } else {
        mc_socket_t sock = read_ac->second;
        read_ac.release();
        return lookup(sock);
    }
}

int
CMMSocketImpl::mc_close(mc_socket_t sock)
{
    VanillaListenerSet::accessor listener_ac;
    CMMSocketImplPtr sk;
    if (cmm_sock_hash.find(sock, sk)) {
        {
            PthreadScopedRWLock lock(&sk->my_lock, false);
            sk->goodbye(false);
            shutdown(sk->select_pipe[0], SHUT_RDWR);
            shutdown(sk->select_pipe[1], SHUT_RDWR);
        }

        pthread_mutex_lock(&hashmaps_mutex);
	cmm_sock_hash.erase(sock);
        /* the CMMSocket object gets destroyed by the shared_ptr. */
        /* moved the rest of the cleanup to the destructor */
        pthread_mutex_unlock(&hashmaps_mutex);
	return 0;
    } else if (cmm_listeners.find(listener_ac, sock)) {
        pthread_mutex_lock(&hashmaps_mutex);
        cmm_listeners.erase(listener_ac);
        pthread_mutex_unlock(&hashmaps_mutex);
        close(sock);
        return 0;
    } else {
	dbgprintf("Warning: cmm_close()ing a socket that's not "
                  "in my hash\n");
        return close(sock);
    }
}

CMMSocketImpl::CMMSocketImpl(int family, int type, int protocol)
    : bootstrapper(NULL),
      listener_thread(NULL),
      remote_listener_port(0),
      shutting_down(false),
      remote_shutdown(false),
      goodbye_sent(false),
      incoming_irobs(this),
      irob_indexes(0),
      sending_goodbye(false)
{
    // gets updated on first FG activity
    last_fg.tv_sec = last_fg.tv_usec = 0;

//     total_inter_fg_time.tv_sec = total_inter_fg_time.tv_usec = 0;
//     fg_count = 0;

    /* reserve a dummy OS file descriptor for this mc_socket. */
    sock = socket(family, type, protocol);
    if (sock < 0) {
	/* invalid params, or no more FDs/memory left. */
	throw sock; /* :-) */
    }

    int rc = socketpair(AF_UNIX, SOCK_STREAM, 0, select_pipe);
    if (rc < 0) { 
        perror("socketpair");
        close(sock);
        throw rc;
    }
    set_nonblocking(select_pipe[0]);
    set_nonblocking(select_pipe[1]);

    rc = socketpair(AF_UNIX, SOCK_STREAM, 0, write_ready_pipe);
    if (rc < 0) {
        perror("socketpair");
        close(select_pipe[0]);
        close(select_pipe[1]);
        close(sock);
        throw rc;
    }
    set_nonblocking(write_ready_pipe[0]);
    set_nonblocking(write_ready_pipe[1]);

    /* so we can identify this FD as a mc_socket later */
    /* XXX: This won't work when we yank out Juggler. */
    //set_socket_labels(sock, FAKE_SOCKET_MAGIC_LABELS);

    /* XXX: think about how to support things besides
     * (PF_INET, SOCK_STREAM, 0) e.g. PF_INET6? SOCK_DGRAM? 
     * (More for library robustness than interesting research.) 
     */
    sock_family = family;
    sock_type = type;
    sock_protocol = protocol;

    non_blocking = false;

    csock_map = NULL;
    //csock_map = new CSockMapping(this);
    pthread_mutex_init(&scheduling_state_lock, NULL);
    pthread_cond_init(&scheduling_state_cv, NULL);
    
    pthread_rwlock_init(&my_lock, NULL);
}

CMMSocketImpl::~CMMSocketImpl()
{
    dbgprintf("multisocket %d is being destroyed\n", sock);
    cancel_all_thunks(sock);

    delete csock_map;

    bootstrapper->join();
    delete bootstrapper;

    {
        // XXX: ugly.  maybe it can be made more like the bootstrapper.
        PthreadScopedLock lock(&scheduling_state_lock);
        if (listener_thread) {
            listener_thread->stop();
            while (listener_thread) {
                pthread_cond_wait(&scheduling_state_cv, &scheduling_state_lock);
            }
        }
    }
    //delete listener_thread;
    //  it deletes itself now, just before it exits.
    
    //free(remote_addr);
    close(select_pipe[0]);
    close(select_pipe[1]);
    close(write_ready_pipe[0]);
    close(write_ready_pipe[1]);
    close(sock);

    pthread_mutex_destroy(&scheduling_state_lock);
    pthread_cond_destroy(&scheduling_state_cv);
}

int 
CMMSocketImpl::mc_connect(const struct sockaddr *serv_addr, 
                          socklen_t addrlen)
{
    {    
        CMMSocketImplPtr sk;
	if (!cmm_sock_hash.find(sock, sk)) {
            assert(0);
	}
    }

    if (bootstrapper) {
        int conn_status = connect_status();
        if (conn_status == 0) {
            errno = EISCONN;
        } else if (conn_status == EINPROGRESS) {
            errno = EALREADY;
        }
        return CMM_FAILED;
    }

    if (!serv_addr) {
        errno = EINVAL;
        return CMM_FAILED;
    }

    
    int rc = connection_bootstrap(serv_addr, addrlen);
    
    return rc;
}

/* these are all sockopts that have succeeded in the past. 
 * for now, let's assume they succeed again. 
 * this may be invalid; maybe some sockopts succeed on one interface
 * but fail on another?  not sure. XXX */
/* REQ: call with write lock on this cmm_sock */
int 
CMMSocketImpl::set_all_sockopts(int osfd)
{
    if (osfd != -1) {
	for (SockOptHash::const_iterator i = sockopts.begin(); i != sockopts.end(); i++) {
	    int level = i->first;
	    const SockOptNames &optnames = i->second;
	    for (SockOptNames::const_iterator j = optnames.begin();
		 j != optnames.end(); j++) {
		int optname = j->first;
		const struct sockopt &opt = j->second;
		int rc = setsockopt(osfd, level, optname, 
				    opt.optval, opt.optlen);
		if (rc < 0) {
		    return rc;
		}
	    }
	}
    }
    
    return 0;
}

void
CMMSocketImpl::get_fds_for_select(mcSocketOsfdPairList &osfd_list,
                                  bool reading, bool writing)
{
    if (reading) {
        PthreadScopedRWLock lock(&my_lock, true);
        assert(!writing);
        osfd_list.push_back(make_pair(sock, select_pipe[0]));
        clear_select_pipe(select_pipe[0], true);
        if (incoming_irobs.data_is_ready()) {
            // select can return now, so make sure it does
            char c = 42; // value will be ignored
            (void)send(select_pipe[1], &c, 1, MSG_NOSIGNAL);
            /* if this write fails, then either we're shutting down or the
             * buffer is full.  No big deal either way. */
            dbgprintf("read-selecting on msocket %d, which has data ready\n",
                      sock);
        } else {
            dbgprintf("read-selecting on msocket %d, no data ready yet\n",
                      sock);
        }
        dbgprintf("Swapped msocket fd %d for select_pipe input, fd %d\n",
                  sock, select_pipe[0]);
    } else if (writing) {
        PthreadScopedRWLock lock(&my_lock, true);
        assert(!reading);
        osfd_list.push_back(make_pair(sock, write_ready_pipe[0]));
        if (bootstrapper->status() != EINPROGRESS) {
            char c = 42;
            (void)send(write_ready_pipe[1], &c, 1, MSG_NOSIGNAL);
            dbgprintf("write-selecting on msocket %d; won't block\n", sock);
        } else {
            dbgprintf("write-selecting on msocket %d; blocking\n", sock);
        }
        dbgprintf("Swapped msocket fd %d for write_ready_pipe input, fd %d\n",
                  sock, write_ready_pipe[0]);
    } else {
        PthreadScopedRWLock lock(&my_lock, false);
        // not reading, not writing; this must be for xcptfds
        // XXX: what about some fakery here?
        csock_map->get_real_fds(osfd_list);
    }
}

int 
CMMSocketImpl::make_real_fd_set(int nfds, fd_set *fds,
				mcSocketOsfdPairList &osfd_list, 
				int *maxosfd, 
                                bool reading, bool writing)
{
    if (!fds) {
	return 0;
    }

    //fprintf(stderr, "DBG: about to check fd_set %p for mc_sockets\n", fds);
    for (mc_socket_t s = nfds - 1; s >= 0; s--) {
        //fprintf(stderr, "DBG: checking fd %d\n", s);
	if (FD_ISSET(s, fds)) {
            //fprintf(stderr, "DBG: fd %d is set\n", s);
	    CMMSocketImplPtr sk;
	    if (!cmm_sock_hash.find(s, sk)) {
                /* This must be a real file descriptor, not a mc_socket. 
                 * No translation needed. */
                continue;
	    }

	    FD_CLR(s, fds);
	    assert(sk);
            // lock only needed for get_fds_for_select, which now
            //   does its own locking
            //PthreadScopedRWLock lock(&sk->my_lock, false);
	    sk->get_fds_for_select(osfd_list, reading, writing);
	}
        
        
    }

    if (osfd_list.size() == 0) {
	return 0;
    }

    assert (maxosfd);
    for (size_t i = 0; i < osfd_list.size(); i++) {
        FD_CLR(osfd_list[i].first, fds);
	FD_SET(osfd_list[i].second, fds);
        if (osfd_list[i].second > *maxosfd) {
            *maxosfd = osfd_list[i].second;
        }
    }
    return 0;
}

/* translate osfds back to mc_sockets.  Return the number of
 * duplicate mc_sockets. */
int 
CMMSocketImpl::make_mc_fd_set(fd_set *fds, 
			      const mcSocketOsfdPairList &osfd_list)
{
    int dups = 0;
    if (!fds) {
	return 0;
    }

    for (size_t j = 0; j < osfd_list.size(); j++) {
	if (FD_ISSET(osfd_list[j].second, fds)) {
            /* this works because mc_socket fds and osfds never overlap */
	    FD_CLR(osfd_list[j].second, fds);
            if (FD_ISSET(osfd_list[j].first, fds)) {
                dups++;
            } else {
                FD_SET(osfd_list[j].first, fds);
                dbgprintf("Mapped osfd %d back to msocket %d\n",
                          osfd_list[j].second, osfd_list[j].first);
            }
	}
    }

    return dups;
}

int 
CMMSocketImpl::mc_select(mc_socket_t nfds, 
			 fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
			 struct timeval *timeout)
{
    int maxosfd = nfds - 1;
    int rc;

    /* these lists will be populated with the mc_socket mappings
     * that were in the original fd_sets */
    mcSocketOsfdPairList readosfd_list;
    mcSocketOsfdPairList writeosfd_list;
    mcSocketOsfdPairList exceptosfd_list;

    rc = 0;
    fd_set tmp_readfds, tmp_writefds, tmp_exceptfds;
    FD_ZERO(&tmp_readfds);
    FD_ZERO(&tmp_writefds);
    FD_ZERO(&tmp_exceptfds);

    dbgprintf("libcmm: mc_select: making real fd_sets\n");

    if (exceptfds) {
	tmp_exceptfds = *exceptfds;
	rc = make_real_fd_set(nfds, &tmp_exceptfds, exceptosfd_list,&maxosfd, false, false);
        if (rc < 0) {
            return -1;
        }
    }

    if (writefds) {
	tmp_writefds = *writefds; 
	rc = make_real_fd_set(nfds, &tmp_writefds, writeosfd_list, &maxosfd, false, true);
        if (rc < 0) {
            return -1;
        }
    }
    if (readfds) {
	tmp_readfds = *readfds;
	rc = make_real_fd_set(nfds, &tmp_readfds, readosfd_list, &maxosfd, true, false);
        if (rc < 0) {
            return -1;
        }
    }

    // any write_ready_pipe[0] fds in tmp_writefds really belong in
    //  tmp_readfds when I call select()
    // (they'll be mapped back to writefds afterwards)
    for (size_t i = 0; i < writeosfd_list.size(); ++i) {
        int fd = writeosfd_list[i].second;
        assert(FD_ISSET(fd, &tmp_writefds));
        FD_CLR(fd, &tmp_writefds);
        FD_SET(fd, &tmp_readfds);
    }

    dbgprintf("libcmm: about to call select(), maxosfd=%d\n", maxosfd);

    rc = select(maxosfd + 1, &tmp_readfds, &tmp_writefds, &tmp_exceptfds, 
		timeout);

    dbgprintf("libcmm: returned from select()\n");
    
    if (rc < 0) {
	/* select does not modify the fd_sets if failure occurs */
	return rc;
    }

    for (size_t i = 0; i < writeosfd_list.size(); ++i) {
        int fd = writeosfd_list[i].second;
        if (FD_ISSET(fd, &tmp_readfds)) {
            FD_SET(fd, &tmp_writefds);
            FD_CLR(fd, &tmp_readfds);
        }
    }

    /* map osfds back to mc_sockets, and correct for duplicates */
    rc -= make_mc_fd_set(&tmp_readfds, readosfd_list);
    rc -= make_mc_fd_set(&tmp_writefds, writeosfd_list);
    rc -= make_mc_fd_set(&tmp_exceptfds, exceptosfd_list);

    for (int i = 0; i < nfds; ++i) {
        if (FD_ISSET(i, &tmp_readfds) ||
            FD_ISSET(i, &tmp_writefds)) {

            CMMSocketImplPtr sk;
            if (!cmm_sock_hash.find(i, sk)) {
                /* This must be a real file descriptor, 
                 * not a mc_socket.  Skip it. */
                continue;
            }
            
            assert(sk);

            if (FD_ISSET(i, &tmp_readfds)) {
                dbgprintf("SELECT_DEBUG fd %d is set in tmp_readfds\n", i);
                sk->clear_select_pipe(sk->select_pipe[0]);
            }
            if (FD_ISSET(i, &tmp_writefds)) {
                dbgprintf("SELECT_DEBUG fd %d is set in tmp_writefds\n", i);
                sk->clear_select_pipe(sk->write_ready_pipe[0]);
            }
        }
    }
    
    if (readfds)   { *readfds   = tmp_readfds;   }
    if (writefds)  { *writefds  = tmp_writefds;  }
    if (exceptfds) { *exceptfds = tmp_exceptfds; }

    return rc;
}

void
CMMSocketImpl::clear_select_pipe(int fd, bool already_locked)
{
    auto_ptr<PthreadScopedRWLock> lock_ptr;
    if (!already_locked) {
        lock_ptr.reset(new PthreadScopedRWLock(&my_lock, true));
    }

    char junk[64];
    int bytes_cleared = 0;
    dbgprintf("Emptying select pipe %d for msocket %d\n", fd, sock);
    int ret = read(fd, &junk, 64);
    while (ret > 0) {
        bytes_cleared += ret;
        // empty the pipe so future select()s have to
        //  check the incoming_irobs structure
        ret = read(fd, &junk, 64);
    }
    dbgprintf("Cleared out %d bytes for msocket %d\n",
              bytes_cleared, sock);
}

int 
CMMSocketImpl::mc_poll(struct pollfd fds[], nfds_t nfds, int timeout)
{
    /* maps osfds to pointers into the original fds array */
    map<int, struct pollfd*> osfds_to_pollfds;
    vector<struct pollfd> real_fds_list;

    dbgprintf("mc_poll with %lu fds [ ", nfds);
    for(nfds_t i=0; i<nfds; i++) {
        dbgprintf_plain("%d%s%s ", fds[i].fd,
                        (fds[i].events & POLLIN)?"i":"",
                        (fds[i].events & POLLOUT)?"o":"");
        fds[i].revents = 0;
    }
    dbgprintf_plain("]\n");

    // contains the write_ready_pipe fds that are to be
    //   POLLIN'd (in order to POLLOUT on a multisocket)
    //set<int> write_fds;

    for(nfds_t i=0; i<nfds; i++) {
	mcSocketOsfdPairList osfd_list;
        CMMSocketImplPtr sk;
	if(!cmm_sock_hash.find(fds[i].fd, sk)) {
	    real_fds_list.push_back(fds[i]);
	    osfds_to_pollfds[fds[i].fd] = &fds[i];
	    continue; //this is a non mc_socket
	} else {
	    assert(sk);
            // lock only needed for get_fds_for_select, which now
            //   does its own locking
            //PthreadScopedRWLock lock(&sk->my_lock, false); 
	    //sk->csock_map->get_real_fds(osfd_list);
            if (fds[i].events & POLLIN) {
                sk->get_fds_for_select(osfd_list, true, false);
            }
            if (fds[i].events & POLLOUT) {
                sk->get_fds_for_select(osfd_list, false, true);
                /*
                mcSocketOsfdPairList tmp_list;
                sk->get_fds_for_select(tmp_list, false, true);
                for (size_t i = 0; i < tmp_list.size(); ++i) {
                    //int fd = tmp_list[i].second;
                    //write_fds.insert(fd);
                    osfd_list.push_back(tmp_list[i]);
                }
                */
            }
	    if (osfd_list.size() == 0) {
		/* XXX: is this right? should we instead
		 * wait for connections to poll on? */
		errno = ENOTCONN;
		return -1;
	    }
	    for (size_t j = 0; j < osfd_list.size(); j++) {
		/* copy struct pollfd, overwrite fd */
		real_fds_list.push_back(fds[i]);
                struct pollfd& real_fd = real_fds_list.back();
		real_fd.fd = osfd_list[j].second;

                // a POLLOUT poll request on a multisocket
                //  must be translated to a POLLIN request 
                //  on its write_ready_pipe[0]
                //  (and back again later)
                if (real_fd.fd == sk->select_pipe[0]) {
                    // strip flags that are not POLLIN
                    assert(real_fd.events & POLLIN);
                    real_fd.events = POLLIN;
                } else if (real_fd.fd == sk->write_ready_pipe[0]) { 
                    //write_fds.count(real_fd.fd) == 1) {
                    assert(real_fd.events & POLLOUT);
                    //real_fd.events &= ~POLLOUT;
                    //real_fd.events |= POLLIN;
                    real_fd.events = POLLIN;
                }
		osfds_to_pollfds[osfd_list[j].second] = &fds[i];
	    }
	}
    }

    nfds_t real_nfds = real_fds_list.size();
    struct pollfd *realfds = new struct pollfd[real_nfds];
    dbgprintf("About to call poll(): %lu fds [ ",
              real_nfds);
    for (nfds_t i = 0; i < real_nfds; i++) {
	realfds[i] = real_fds_list[i];
        dbgprintf_plain("%d%s%s ", realfds[i].fd,
                        (realfds[i].events & POLLIN)?"i":"",
                        (realfds[i].events & POLLOUT)?"o":"");
    }
    dbgprintf_plain("]\n");

    int rc = poll(realfds, real_nfds, timeout);
    dbgprintf("poll() returns %d: %lu fds [ ",
              rc, real_nfds);
    for(nfds_t i=0; i < real_nfds; i++) {
        dbgprintf_plain("%d%s%s ", realfds[i].fd,
                        (realfds[i].revents & POLLIN)?"i":"",
                        (realfds[i].revents & POLLOUT)?"o":"");
    }
    dbgprintf_plain("]\n");
    if (rc <= 0) {
        delete [] realfds;
	return rc;
    }

    //int lastfd = -1;
    set<int> orig_fds;
    for (nfds_t i = 0; i < real_nfds; i++) {
	struct pollfd *origfd = osfds_to_pollfds[realfds[i].fd];
	assert(origfd);
        CMMSocketImplPtr sk;
	if(!cmm_sock_hash.find(fds[i].fd, sk)) {
	    origfd->revents = realfds[i].revents;
	} else {
            //CMMSocketImplPtr sk = ac->second;
	    //assert(sk);
	    //sk->poll_map_back(origfd, &realfds[i]);

            // if a read event happened on write_ready_pipe[0],
            // it really means that POLLOUT happened on the multisocket
            if (realfds[i].fd == sk->write_ready_pipe[0]) { 
                //write_fds.count(realfds[i].fd) == 1) {
                if (realfds[i].revents & POLLIN) {
                    //realfds[i].revents &= ~POLLIN;
                    realfds[i].revents = POLLOUT;
                }
            }

            origfd->revents |= realfds[i].revents;
            if (origfd->revents & POLLIN) {
                dbgprintf("multisocket %d has POLLIN revent\n",
                          sk->sock);
                sk->clear_select_pipe(sk->select_pipe[0]);
            }
            if (origfd->revents & POLLOUT) {
                dbgprintf("multisocket %d has POLLOUT revent\n",
                          sk->sock);
                sk->clear_select_pipe(sk->write_ready_pipe[0]);
            }
	}
        if (origfd->revents & (POLLIN | POLLOUT)) {
            if (orig_fds.find(origfd->fd) == orig_fds.end()) {
                orig_fds.insert(origfd->fd);
            } else {
                /* correct return value for duplicates */
                rc--;
            }
        }
    }
    delete [] realfds;

    dbgprintf("Returning %d from mc_poll(): %lu fds [ ",
              rc, nfds);
    for(nfds_t i=0; i<nfds; i++) {
        dbgprintf_plain("%d%s%s ", fds[i].fd,
                        (fds[i].revents & POLLIN)?"i":"",
                        (fds[i].revents & POLLOUT)?"o":"");
    }
    dbgprintf_plain("]\n");
    return rc;
}

ssize_t 
CMMSocketImpl::mc_send(const void *buf, size_t len, int flags,
		       u_long send_labels, 
		       resume_handler_t resume_handler, void *arg)
{
    if ((ssize_t)len < 0) {
	errno = EINVAL;
	return -1;
    }
    
    int cstatus = connect_status();
    if (cstatus != 0) {
        if (cstatus == EINPROGRESS) {
            errno = EAGAIN;
        }  else {
            errno = ENOTCONN;
        }
        return -1;
    }

    struct timeval begin, end, diff;
    TIME(begin);
    
    irob_id_t id = -1;
    {
        PthreadScopedLock maps_lock(&hashmaps_mutex);
        id = g_next_irob++;
    }
    PthreadScopedRWLock sock_lock(&my_lock, false);

    int rc = default_irob(id, buf, len, flags,
                          send_labels, 
                          resume_handler, arg);

    TIME(end);
    TIMEDIFF(begin, end, diff);
    dbgprintf("mc_send (%d bytes) took %lu.%06lu seconds, start-to-finish\n", 
	      rc, diff.tv_sec, diff.tv_usec);

#ifdef CMM_TIMING
    if (rc > 0) {
        PthreadScopedLock lock(&timing_mutex);
        if (timing_file) {
            struct timeval now;
            TIME(now);
            fprintf(timing_file, "%lu.%06lu IROB %ld %d bytes enqueued with label %lu in %lu.%06lu seconds multisocket %d\n", 
                    now.tv_sec, now.tv_usec, id, rc, send_labels, diff.tv_sec, diff.tv_usec, sock);
        }
        //global_stats.bytes_sent[send_labels] += rc;
        //global_stats.send_count[send_labels]++;
    }
#endif
    return rc;
}

int 
CMMSocketImpl::mc_writev(const struct iovec *vec, int count,
			 u_long send_labels, 
                         resume_handler_t resume_handler, void *arg)
{
    if (count < 0) {
	errno = EINVAL;
	return -1;
    } else if (count == 0) {
	return 0;
    }

    int cstatus = connect_status();
    if (cstatus != 0) {
        if (cstatus == EINPROGRESS) {
            errno = EAGAIN;
        }  else {
            errno = ENOTCONN;
        }
        return -1;
    }

    ssize_t total_bytes = 0;
    for (int i = 0; i < count; i++) {
	ssize_t bytes = total_bytes;
	if (vec[i].iov_len < 0) {
	    errno = EINVAL;
	    return -1;
	}
	total_bytes += vec[i].iov_len;
	if (total_bytes < bytes) {
	    /* overflow */
	    errno = EINVAL;
	    return -1;
	}
    }

    struct timeval begin, end, diff;
    TIME(begin);
    
    irob_id_t id = -1;
    {
        PthreadScopedLock maps_lock(&hashmaps_mutex);
        id = g_next_irob++;
    }
    PthreadScopedRWLock sock_lock(&my_lock, false);

    dbgprintf("Calling default_irob with %d bytes\n", total_bytes);
    int rc = default_irob_writev(id, vec, count, total_bytes,
                                 send_labels, 
                                 resume_handler, arg);

    TIME(end);
    TIMEDIFF(begin, end, diff);
    dbgprintf("mc_writev (%d bytes) took %lu.%06lu seconds, start-to-finish\n", 
	      rc, diff.tv_sec, diff.tv_usec);
    
#ifdef CMM_TIMING
    if (rc > 0) {
        PthreadScopedLock lock(&timing_mutex);
        if (timing_file) {
            struct timeval now;
            TIME(now);
            fprintf(timing_file, "%lu.%06lu IROB %ld %d bytes enqueued with label %lu in %lu.%06lu seconds multisocket %d\n", 
                    now.tv_sec, now.tv_usec, id, rc, send_labels, diff.tv_sec, diff.tv_usec, sock);
        }
        //global_stats.bytes_sent[send_labels] += rc;
        //global_stats.send_count[send_labels]++;
    }
#endif
    return rc;
}

struct shutdown_each {
    int how;
    shutdown_each(int how_) : how(how_) {}

    int operator()(CSocketPtr csock) {
	assert(csock);
	assert(csock->osfd >= 0);
	return shutdown(csock->osfd, how);
    }
};

int
CMMSocketImpl::mc_shutdown(int how)
{
    int rc = 0;
    PthreadScopedRWLock lock(&my_lock, false);
    goodbye(false);
    if (how == SHUT_RD || how == SHUT_RDWR) {
        shutdown(select_pipe[0], SHUT_RDWR);
        shutdown(select_pipe[1], SHUT_RDWR);
    }
    if (how == SHUT_WR || how == SHUT_RDWR) {
        shutdown(write_ready_pipe[0], SHUT_RDWR);
        shutdown(write_ready_pipe[1], SHUT_RDWR);
    }
    //rc = csock_map->for_each(shutdown_each(how));

    return rc;
}

irob_id_t 
CMMSocketImpl::mc_begin_irob(int numdeps, const irob_id_t *deps, 
                             u_long send_labels, 
                             resume_handler_t rh, void *rh_arg)
{
    irob_id_t id = -1;
    {
        PthreadScopedLock maps_lock(&hashmaps_mutex);
        id = g_next_irob++;
    }
    PthreadScopedRWLock sock_lock(&my_lock, false);
    int rc = begin_irob(id, numdeps, deps, 
                        send_labels, 
                        rh, rh_arg);
    if (rc < 0) {
        return rc;
    }
    IROBSockHash::accessor ac;
    if (!irob_sock_hash.insert(ac, id)) {
	assert(0);
    }
    ac->second = sock;

    return id;
}

int
CMMSocketImpl::mc_end_irob(irob_id_t id)
{
    PthreadScopedRWLock sock_lock(&my_lock, false);
    int rc = end_irob(id);
    if (rc == 0) {
	irob_sock_hash.erase(id);
    }
    return rc;
}

ssize_t
CMMSocketImpl::mc_irob_send(irob_id_t id, 
                            const void *buf, size_t len, int flags)
{
    PthreadScopedRWLock sock_lock(&my_lock, false);
    return irob_chunk(id, buf, len, flags);
}

int
CMMSocketImpl::mc_irob_writev(irob_id_t id, 
                              const struct iovec *vec, int count)
{
    /* XXX: gratuitous copying of bytes. */

    if (!vec || count <= 0) {
        errno = EINVAL;
        return -1;
    }

    int buflen = 0;
    for (int i = 0; i < count; i++) {
        if (vec[i].iov_len <= 0) {
            errno = EINVAL;
            return -1;
        }
        buflen += vec[i].iov_len;
    }
    char *buf = new char[buflen];
    int bytes_copied = 0;
    for (int i = 0; i < count; i++) {
        memcpy(buf + bytes_copied, vec[i].iov_base, vec[i].iov_len);
        bytes_copied += vec[i].iov_len;
    }
    assert(bytes_copied == buflen);

    long rc = mc_irob_send(id, buf, buflen, 0);
    delete [] buf;
    return rc;
}


void
CMMSocketImpl::interface_up(struct net_interface up_iface)
{
#ifdef CMM_TIMING
    {
        PthreadScopedLock lock(&timing_mutex);
        if (timing_file) {
            struct timeval now;
            TIME(now);
            fprintf(timing_file, "%lu.%06lu  Bringing up %s, bw_down %lu bw_up %lu rtt %lu\n",
		    now.tv_sec, now.tv_usec, inet_ntoa(up_iface.ip_addr),
		    up_iface.bandwidth_down, up_iface.bandwidth_up, up_iface.RTT);
        }
    }
#endif

    pthread_mutex_lock(&hashmaps_mutex);

    dbgprintf("Bringing up %s, label %lu\n",
              inet_ntoa(up_iface.ip_addr), up_iface.labels);
    
    ifaces.insert(up_iface);

    for (CMMSockHash::iterator sk_iter = cmm_sock_hash.begin();
	 sk_iter != cmm_sock_hash.end(); sk_iter++) {
	CMMSocketImplPtr sk = sk_iter->second;
	assert(sk);

        sk->setup(up_iface, true);
    }
    pthread_mutex_unlock(&hashmaps_mutex);
}

void
CMMSocketImpl::interface_down(struct net_interface down_iface)
{
#ifdef CMM_TIMING
    {
        PthreadScopedLock lock(&timing_mutex);
        if (timing_file) {
            struct timeval now;
            TIME(now);
            fprintf(timing_file, "%lu.%06lu  Bringing down %s\n",
		    now.tv_sec, now.tv_usec, inet_ntoa(down_iface.ip_addr));
        }
    }
#endif

    pthread_mutex_lock(&hashmaps_mutex);

    dbgprintf("Bringing down %s, label %lu\n",
              inet_ntoa(down_iface.ip_addr), down_iface.labels);
    ifaces.erase(down_iface);

    /* put down the sockets connected on now-unavailable networks. */
    for (CMMSockHash::iterator sk_iter = cmm_sock_hash.begin();
	 sk_iter != cmm_sock_hash.end(); sk_iter++) {
	CMMSocketImplPtr sk = sk_iter->second;
	assert(sk);

	sk->teardown(down_iface, true);
    }
    pthread_mutex_unlock(&hashmaps_mutex);
}

int 
CMMSocketImpl::mc_listen(int listener_sock, int backlog)
{
    struct sockaddr addr;
    socklen_t len = sizeof(addr);
    int gsn_rc = getsockname(listener_sock, (struct sockaddr *)&addr, &len);
    if (gsn_rc < 0) {
        dbgprintf("mc_listen: getsockname failed, errno=%d\n", errno);
        if (errno != ENOTSOCK) {
            errno = EBADF;
        }
        return -1;
    } else {
        int rc = listen(listener_sock, backlog);
        if (rc < 0) {
            return rc;
        }

        if (addr.sa_family != AF_INET) {
            // do pass-through for all other socket types,
            // since we only support AF_INET right now
            dbgprintf("Warning: only AF_INET supported.  cmm_listen returns "
                      "pass-through listen() for AF %d.\n", addr.sa_family);
            return rc;
        }
    }

    pthread_mutex_lock(&hashmaps_mutex);
    {
        VanillaListenerSet::accessor ac;
        (void)cmm_listeners.insert(ac, listener_sock);
    }
    pthread_mutex_unlock(&hashmaps_mutex);
    return 0;
}

mc_socket_t 
CMMSocketImpl::mc_accept(int listener_sock, 
                         struct sockaddr *addr, socklen_t *addrlen)
{
    if (!scout_ipc_inited()) {
        errno = EPROTO; // XXX: maybe? 
        return -1;
    }

    VanillaListenerSet::const_accessor ac;
    if (!cmm_listeners.find(ac, listener_sock)) {
        /* pass-through */
        dbgprintf("cmm_accept returning pass-through accept() "
                  "for listener_sock %d\n", listener_sock);
        return accept(listener_sock, addr, addrlen);
    }

    int sock = accept(listener_sock, addr, addrlen);
    if (sock < 0) {
        return sock;
    }
    ac.release();

    struct sockaddr_in *ip_sockaddr = (struct sockaddr_in *)addr;
    dbgprintf("mc_accept: Accepting connection from %s\n",
              inet_ntoa(ip_sockaddr->sin_addr));
    
    mc_socket_t mc_sock = CMMSocketImpl::create(PF_INET, SOCK_STREAM, 0);
    CMMSocketPtr sk = CMMSocketImpl::lookup(mc_sock);
    CMMSocketImpl *sk_impl = dynamic_cast<CMMSocketImpl*>(get_pointer(sk));
    assert(sk_impl);
    int rc = sk_impl->connection_bootstrap(addr, *addrlen, sock);
    //close(sock);
        
    if (rc < 0) {
        dbgprintf("mc_accept: failed to bootstrap incoming connection\n");
        mc_close(mc_sock);
        return rc;
    }
    return mc_sock;
}

int 
CMMSocketImpl::mc_recv(void *buf, size_t count, int flags,
                       u_long *recv_labels)
{
    //CMMSockHash::const_accessor ac;
    //read_lock(ac);
    // Not needed; incoming_irobs.recv is thread-safe.

    int cstatus = connect_status();
    if (cstatus != 0) {
        if (cstatus == EINPROGRESS) {
            errno = EAGAIN;
        }  else {
            errno = ENOTCONN;
        }
        return -1;
    }

    struct timeval begin, end, diff;
    TIME(begin);
    int rc = incoming_irobs.recv(buf, count, flags, recv_labels);
    TIME(end);
    TIMEDIFF(begin, end, diff);
    dbgprintf("mc_read (%d bytes) took %lu.%06lu seconds, start-to-finish\n", 
	      rc, diff.tv_sec, diff.tv_usec);

    return rc;
}

int 
CMMSocketImpl::mc_getsockopt(int level, int optname, 
                             void *optval, socklen_t *optlen)
{
    PthreadScopedRWLock lock(&my_lock, false);

    if (optname == SO_ERROR) {
        // note that this doesn't clear the error
        // like normal SO_ERROR does.  Shouldn't matter;
        // once there's an error of this sort, can't do
        // much besides close the socket.
        if (!optval || !optlen) {
            errno = EFAULT;
            return -1;
        }
        if (level != SOL_SOCKET) {
            errno = ENOPROTOOPT;
            return -1;
        }
        *optlen = sizeof(int);
        *((int*)optval) = bootstrapper ? bootstrapper->status() : 0;
        return 0;
    }

    CSocketPtr csock = csock_map->csock_with_labels(0);
    if (csock) {
	return getsockopt(csock->osfd, level, optname, optval, optlen);
    } else {
        struct sockopt &opt = sockopts[level][optname];
        if (opt.optval) {
            *optlen = opt.optlen;
            memcpy(optval, opt.optval, opt.optlen);
            return 0;
        } else {
            /* last resort; we haven't set this opt on this socket before,
	     * and we don't have any connected sockets right now,
             * so just return the default for the dummy socket */
            return getsockopt(sock, level, optname, optval, optlen);
        }
    }
}

struct set_sock_opt {
    int level;
    int optname;
    const void *optval;
    socklen_t optlen;
    set_sock_opt(int l, int o, const void *v, socklen_t len)
	: level(l), optname(0), optval(v), optlen(len) {}

    int operator()(CSocketPtr csock) {
	assert(csock);
	assert(csock->osfd >= 0);

        if(optname == O_NONBLOCK) {
            int flags;
            flags = fcntl(csock->osfd, F_GETFL, 0);
            flags |= O_NONBLOCK;
            return fcntl(csock->osfd, F_SETFL, flags);
        } else {
            return setsockopt(csock->osfd, level, optname, optval, optlen);
        }
    }
};

bool
CMMSocketImpl::is_non_blocking()
{
    return (non_blocking ||
            (fcntl(sock, F_GETFL, 0) & O_NONBLOCK));
}

int
CMMSocketImpl::mc_setsockopt(int level, int optname, 
                             const void *optval, socklen_t optlen)
{
    int rc = 0;
    PthreadScopedRWLock lock(&my_lock, true);

    if (optname == O_NONBLOCK) {
	non_blocking = true;
    }

    if (optname != O_NONBLOCK) {
        rc = setsockopt(sock, level, optname, optval, optlen);
        if (rc < 0) {
            dbgprintf("warning: failed setting socket option on "
                      "dummy socket\n");
        }

        rc = csock_map->for_each(set_sock_opt(level, optname, optval, optlen));
        if (rc < 0) {
            return rc;
        }
        /* all succeeded */

        /* inserts if not present */
        struct sockopt &opt = sockopts[level][optname];
        if (opt.optval) {
            free(opt.optval);
        }
        opt.optlen = optlen;
        opt.optval = malloc(optlen);
        assert(opt.optval);
        memcpy(opt.optval, optval, optlen);
    } else {
        // don't call setsockopt for the real sockets; the library
        //   depends on blocking I/O for them.
        // Instead, we give the illusion of non-blocking I/O
        //   by always returning immediately from the 
        //   relevant calls.
        // This is trivially done for all calls except 
        //   cmm_connect and cmm_accept; they require special care.
    }

    return 0;
}

int 
CMMSocketImpl::mc_getpeername(struct sockaddr *address, 
                              socklen_t *address_len)
{
    PthreadScopedRWLock lock(&my_lock, false);

    CSocketPtr csock = csock_map->csock_with_labels(0);
    if (!csock) {
        /* XXX: maybe instead create a connection and then proceed */
        errno = ENOTCONN;
        return -1;
    } 

    return getpeername(csock->osfd, address, address_len);
}

int 
CMMSocketImpl::mc_getsockname(struct sockaddr *address, 
                              socklen_t *address_len)
{
    PthreadScopedRWLock lock(&my_lock, false);

    CSocketPtr csock = csock_map->csock_with_labels(0);
    if (!csock) {
        return getsockname(sock, address, address_len);
    } 

    return getsockname(csock->osfd, address, address_len);
}

bool
CMMSocketImpl::isLoopbackOnly(bool locked)
{
    auto_ptr<PthreadScopedRWLock> lock_ptr;
    if (locked) {
        lock_ptr.reset(new PthreadScopedRWLock(&my_lock, false));
    }

    if (local_ifaces.size() == 1) {
        struct net_interface the_iface = *local_ifaces.begin();
        if (the_iface.ip_addr.s_addr == htonl(INADDR_LOOPBACK)) {
            return true;
        }
    }
    return false;
}

void
CMMSocketImpl::setup(struct net_interface iface, bool local)
{
    // ignore updates from the scout for a loopback multisocket
    if (isLoopbackOnly()) {
        return;
    }

    PthreadScopedRWLock sock_lock(&my_lock, true);
    
    PthreadScopedLock lock(&scheduling_state_lock);
    bool need_data_check = false;

    NetInterfaceSet& iface_set = local ? local_ifaces : remote_ifaces;
    NetInterfaceSet::const_iterator it = iface_set.find(iface);

    if (it != iface_set.end()) {
        // if bandwidth was previously reported to be zero,
        //  some data probably got dropped.  
        // If the bandwidth is now nonzero, let's check.
        need_data_check = ((it->bandwidth_up == 0 && 
                            iface.bandwidth_up != 0) ||
                           (it->bandwidth_down == 0 && 
                            iface.bandwidth_down != 0));
    }

    // if (local && need_data_check) {
//         bootstrapper->restart(iface, true);
//     }

    // If bootstrapping is in progress, the bootstrapper is
    //  in the middle of creating connections, so don't do it here.
    bool make_connection = (bootstrapper && bootstrapper->status() == 0);
    csock_map->setup(iface, local, make_connection, need_data_check);

    if (local) {
        if (local_ifaces.count(iface) > 0) {
            // make sure labels update if needed
            local_ifaces.erase(iface);
            changed_local_ifaces.erase(iface);

            // in fact, only send a New_Interface message if this
            //  interface is not new.
            changed_local_ifaces.insert(iface);
            pthread_cond_broadcast(&scheduling_state_cv);
        }

        local_ifaces.insert(iface);
    } else {
        if (remote_ifaces.count(iface) > 0) {
            // make sure labels update if needed
            remote_ifaces.erase(iface);
        }
        remote_ifaces.insert(iface);
    }

    if (need_data_check) {
        dbgprintf("Zero-bandwidth period over\n");
        return;
        // try leaving this particular bit of retransmission to TCP.
        
        //  Potential performance improvement: When a new network becomes 
        //  available, do Data_Checks for all unACKed IROBS, hopefully 
        //  proactively retransmitting some data on the new socket,
        //  which has no queued data that TCP is waiting to retransmit.

        /*
        dbgprintf("Recovering from 0-bandwidth period; issuing data-check\n");
        vector<irob_id_t> ids = outgoing_irobs.get_all_ids();
        outgoing_irobs.data_check_all();
        for (size_t i = 0; i < ids.size(); ++i) {
            IROBSchedulingData data_check(ids[i], false);
            irob_indexes.waiting_data_checks.insert(data_check);
        }
        */
    }
}

void
CMMSocketImpl::teardown(struct net_interface iface, bool local)
{
    // ignore updates from the scout for a loopback multisocket
    if (isLoopbackOnly()) {
        return;
    }

    // Restart bootstrapper if it's using this interface
    //  (and if it's still running)
    bootstrapper->restart(iface);

    PthreadScopedRWLock sock_lock(&my_lock, true);
    
    csock_map->teardown(iface, local);

    if (local) {
        local_ifaces.erase(iface);

        PthreadScopedLock lock(&scheduling_state_lock);
        down_local_ifaces.insert(iface);
        pthread_cond_broadcast(&scheduling_state_cv);
    } else {
        remote_ifaces.erase(iface);
    }

    PthreadScopedLock lock(&scheduling_state_lock);
    vector<irob_id_t> ids = outgoing_irobs.get_all_ids();
    outgoing_irobs.data_check_all();
    for (size_t i = 0; i < ids.size(); ++i) {
        IROBSchedulingData data_check(ids[i], false);
        irob_indexes.waiting_data_checks.insert(data_check);
    }

    // even in the remote case, any reading threads need to
    //  notice the csock is gone and wake up, if it's the last one
    pthread_cond_broadcast(&scheduling_state_cv);
}

/* only called with read accessor held on this */
bool
CMMSocketImpl::net_available(u_long send_labels)
{
    struct net_interface local_dummy, remote_dummy;
    return csock_map->get_iface_pair(send_labels, local_dummy, remote_dummy,
                                     false);
}

bool 
CMMSocketImpl::net_available(mc_socket_t sock, 
                             u_long send_labels)
{
    CMMSocketImplPtr sk;
    if (!cmm_sock_hash.find(sock, sk)) {
        return false;
    }
    assert(sk);
    
    PthreadScopedRWLock lock(&sk->my_lock, false);
    return sk->net_available(send_labels);
}


bool
CMMSocketImpl::is_shutting_down()
{
    PthreadScopedLock lock(&scheduling_state_lock);
    return shutting_down;
}

struct BlockingRequest {
    CMMSocketImpl *sk;
    pthread_t tid;

    BlockingRequest(CMMSocketImpl *sk_, pthread_t tid_) 
        : sk(sk_), tid(tid_) {}
};

void unblock_thread_thunk(BlockingRequest *breq)
{
    assert(breq && breq->sk);
    breq->sk->signal_completion(breq->tid, 0);
    delete breq;
}

int
CMMSocketImpl::wait_for_labels(u_long send_labels)
{
    // pseudo-thunk to block this until it's ready to send
    prepare_app_operation();
    BlockingRequest *breq = new BlockingRequest(this, pthread_self());
    enqueue_handler(sock, send_labels, 
                    (resume_handler_t)unblock_thread_thunk, breq);
    int rc = wait_for_completion(send_labels);
    if (rc < 0) {
        cancel_thunk(sock, send_labels,
                     (resume_handler_t)unblock_thread_thunk, breq,
                     delete_arg<BlockingRequest>);
    }
    return rc;
}

/* must call with readlock and scheduling_state_lock held */
int
CMMSocketImpl::get_csock(u_long send_labels, 
                         resume_handler_t resume_handler, void *rh_arg,
                         CSocket *& csock, bool blocking)
{
    try {
        if (net_available(send_labels)) {
            // avoid using sockets that aren't yet connected; if connect() times out,
            //   it might take a long time to send anything
            csock = get_pointer(csock_map->connected_csock_with_labels(send_labels, false));
            if (!csock) {
                csock = get_pointer(csock_map->new_csock_with_labels(send_labels, false));
            }
        } else {
            csock = NULL;
        }

        if (!csock) {
            if (resume_handler) {
                enqueue_handler(sock, send_labels, 
                                resume_handler, rh_arg);
                return CMM_DEFERRED;
            } else {
                if (blocking && !this->is_non_blocking()) {
                    while (!csock) {
                        int rc = wait_for_labels(send_labels);
                        if (rc < 0) {
                            /* timed out */
                            return CMM_FAILED;
                        }
                        csock = get_pointer(csock_map->new_csock_with_labels(send_labels, false));
                    }
                    return 0;
                } else {
                    return CMM_FAILED;
                }
            }
        } else {
            // for background sends, if there's a thunk provided,
            //  check whether the csocket is busy sending any app data.
            // If it is busy, pick a different csocket, or 
            //  register the thunk if there's no free csocket.
            if (send_labels & CMM_LABEL_BACKGROUND &&
                resume_handler) {
                if (csock->is_busy()) {
                    csock = get_pointer(csock_map->get_idle_csock(false));
                    if (!csock) {
                        enqueue_handler(sock, send_labels, 
                                        resume_handler, rh_arg);
                        return CMM_DEFERRED;
                    }
                }
            }

            return 0;
        }
    } catch (std::runtime_error& e) {
        dbgprintf("Error finding csocket by labels: %s\n", e.what());
        return CMM_FAILED;
    }
}

/* This function blocks until a network is available.
 * If the socket is non-blocking, that's not appropriate.  XXX
 */
int
CMMSocketImpl::begin_irob(irob_id_t next_irob, 
                            int numdeps, const irob_id_t *deps,
                            u_long send_labels, 
                            resume_handler_t resume_handler, void *rh_arg)
{
    if (is_shutting_down()) {
	dbgprintf("Tried to begin IROB, but mc_socket %d is shutting down\n", 
		  sock);
	errno = EPIPE;
	return CMM_FAILED;
    }

    struct timeval begin, end, diff;
    TIME(begin);

    irob_id_t id = next_irob;

    CSocket *csock;
    int ret = get_csock(send_labels, resume_handler, rh_arg,
                        csock, true);
    if (ret < 0) {
        return ret;
    }

    PendingSenderIROB *pirob = new PendingSenderIROB(id, numdeps, deps,
                                                     0, NULL,
                                                     send_labels, 
                                                     resume_handler, rh_arg);

    {
        PthreadScopedLock lock(&scheduling_state_lock);
        
        bool success = outgoing_irobs.insert(pirob);
        assert(success);

        if (csock->is_connected()) {
            csock->irob_indexes.new_irobs.insert(IROBSchedulingData(id, false, send_labels));
        } else {
            irob_indexes.new_irobs.insert(IROBSchedulingData(id, false, send_labels));
        }
        pthread_cond_broadcast(&scheduling_state_cv);
    }
    
    TIME(end);
    TIMEDIFF(begin, end, diff);
    dbgprintf("Completed request in %lu.%06lu seconds (begin_irob)\n",
	      diff.tv_sec, diff.tv_usec);
    
    return 0;
}

int
CMMSocketImpl::end_irob(irob_id_t id)
{
    if (is_shutting_down()) {
	dbgprintf("Tried to end IROB, but mc_socket %d is shutting down\n", 
		  sock);
	errno = EPIPE;
	return CMM_FAILED;
    }

    struct timeval begin, end, diff;
    TIME(begin);

    CSocket *csock;
    PendingIROB *pirob = NULL;
    u_long send_labels;
    {
        PthreadScopedLock lock(&scheduling_state_lock);

        pirob = outgoing_irobs.find(id);
        if (!pirob) {
            errno = EINVAL;
            return -1;
        }
        
        if (pirob->is_complete()) {
            dbgprintf("Trying to complete IROB %lu, "
                      "which is already complete\n", id);
            errno = EINVAL;
            return -1;
        }
        send_labels = pirob->send_labels;
    }

    // prefer the IROB's labels, but fall back to any connection
    int ret = get_csock(send_labels,
                        NULL, NULL, csock, false);
    if (ret < 0) {
        send_labels = 0;
        ret = get_csock(0, NULL, NULL, csock, true);
        if (ret < 0) {
            return ret;
        }
    }

    {
        PthreadScopedLock lock(&scheduling_state_lock);
        if (pirob->status < 0) {
            /* last chance to report failure;
             * if the sender has failed to send this IROB's data
             * for some reason, we can still block, thunk or fail. */
            if (pirob->status == CMM_DEFERRED) {
                outgoing_irobs.erase(id);
                delete pirob;
                return CMM_DEFERRED;
            } else {
                pthread_mutex_unlock(&scheduling_state_lock);
                ret = wait_for_labels(send_labels);
                pthread_mutex_lock(&scheduling_state_lock);
                if (ret < 0) {
                    /* timeout; inform the app that the IROB failed */
                    outgoing_irobs.erase(id);
                    delete pirob;
                    return ret;
                }
            }
        }
        pirob->finish();

        PendingSenderIROB *psirob = dynamic_cast<PendingSenderIROB*>(pirob);
        assert(psirob);
        if (psirob->announced && !psirob->end_announced &&
            psirob->is_complete() && psirob->all_chunks_sent()) {
            psirob->end_announced = true;
            if (csock->is_connected()) {
                csock->irob_indexes.finished_irobs.insert(IROBSchedulingData(id, false));
            } else {
                irob_indexes.finished_irobs.insert(IROBSchedulingData(id, false));
            }
            pthread_cond_broadcast(&scheduling_state_cv);
        }
    }
    TIME(end);
    TIMEDIFF(begin, end, diff);
    dbgprintf("Completed request in %lu.%06lu seconds (end_irob)\n",
	      diff.tv_sec, diff.tv_usec);

    return 0;
}


/* This function blocks until a network is available.
 * If the socket is non-blocking, that's not appropriate.  XXX
 */
long
CMMSocketImpl::irob_chunk(irob_id_t id, const void *buf, size_t len, 
                            int flags)
{
    if (is_shutting_down()) {
	dbgprintf("Tried to send IROB chunk, but mc_socket %d is shutting down\n", 
		  sock);
	errno = EPIPE;
	return CMM_FAILED;
    }

    struct timeval begin, end, diff;
    TIME(begin);

    u_long send_labels;
    resume_handler_t resume_handler;
    void *rh_arg;
    CSocket *csock;

    struct irob_chunk_data chunk;
    PendingIROB *pirob = NULL;
    PendingSenderIROB *psirob = NULL;
    {
        PthreadScopedLock lock(&scheduling_state_lock);

        pirob = outgoing_irobs.find(id);
        if (!pirob) {
	    dbgprintf("Tried to add to nonexistent IROB %ld\n", id);
	    throw CMMException();
	}
	
	if (pirob->is_complete()) {
	    dbgprintf("Tried to add to complete IROB %ld\n", id);
	    throw CMMException();
	}

        psirob = dynamic_cast<PendingSenderIROB*>(pirob);
        assert(psirob);
        send_labels = psirob->send_labels;
        resume_handler = psirob->resume_handler;
        rh_arg = psirob->rh_arg;
    }

    // only the begin_irob should try to register a thunk.
    int ret = get_csock(send_labels,
                        NULL, NULL, csock, true);
                       //resume_handler, rh_arg, csock, true);
    if (ret < 0) {
        return ret;
    }
    assert(csock);
    
    {
        PthreadScopedLock lock(&scheduling_state_lock);

	chunk.id = id;
	chunk.seqno = 0; /* will be overwritten 
                          * with new seqno */
	chunk.datalen = len;
	chunk.data = new char[len];
	memcpy(chunk.data, buf, len);

	psirob->add_chunk(chunk); /* writes correct seqno into struct */

        /*
        if (send_labels & CMM_LABEL_ONDEMAND) {
            update_last_fg();
        }
        */

        // XXX: begin and chunk can be out of order now; is this check still needed?
        // XXX: then again, it probably never fails.
        if (psirob->announced) {
            if (csock->is_connected()) {
                csock->irob_indexes.new_chunks.insert(IROBSchedulingData(id, true, send_labels));//chunk.seqno));
            } else {
                irob_indexes.new_chunks.insert(IROBSchedulingData(id, true, send_labels));//chunk.seqno));
            }
            pthread_cond_broadcast(&scheduling_state_cv);
        }
    }
    
    TIME(end);
    TIMEDIFF(begin, end, diff);
    dbgprintf("Completed request in %lu.%06lu seconds (irob_chunk)\n",
	      diff.tv_sec, diff.tv_usec);

#ifdef CMM_TIMING
    {
        PthreadScopedLock lock(&timing_mutex);
        if (timing_file) {
            struct timeval now;
            TIME(now);
            fprintf(timing_file, "%lu.%06lu IROB %ld %u bytes enqueued with label %lu in %lu.%06lu seconds multisocket %d\n", 
                    now.tv_sec, now.tv_usec, id, len, send_labels, diff.tv_sec, diff.tv_usec, sock);
        }
        //global_stats.bytes_sent[send_labels] += rc;
        //global_stats.send_count[send_labels]++;
    }
#endif

    return len;
}

int
CMMSocketImpl::default_irob(irob_id_t next_irob, 
                            const void *buf, size_t len, int flags,
                            u_long send_labels,
                            resume_handler_t resume_handler, void *rh_arg)
{
    struct timeval begin, end, diff;
    TIME(begin);

    CSocket *csock = NULL;
    int rc = validate_default_irob(send_labels, resume_handler, rh_arg, csock);
    if (rc < 0) {
        return rc;
    }

    char *data = new char[len];
    memcpy(data, buf, len);
    rc = send_default_irob(next_irob, csock, data, len, 
                           send_labels, resume_handler, rh_arg);
    
    TIME(end);
    TIMEDIFF(begin, end, diff);
    dbgprintf("Completed request in %lu.%06lu seconds (default_irob_send)\n",
	      diff.tv_sec, diff.tv_usec);

    return rc;
}

int
CMMSocketImpl::default_irob_writev(irob_id_t next_irob, 
                                   const struct iovec *vec, int count, 
                                   ssize_t total_bytes,
                                   u_long send_labels,
                                   resume_handler_t resume_handler, void *rh_arg)
{
    struct timeval begin, end, diff;
    TIME(begin);

    CSocket *csock = NULL;
    int rc = validate_default_irob(send_labels, resume_handler, rh_arg, csock);
    if (rc < 0) {
        return rc;
    }

    char *data = new char[total_bytes];
    ssize_t bytes_copied = 0;
    for (int i = 0; i < count; ++i) {
        memcpy(data + bytes_copied, vec[i].iov_base, vec[i].iov_len);
        bytes_copied += vec[i].iov_len;
        assert(bytes_copied <= total_bytes);
    }
    assert(bytes_copied == total_bytes);

    dbgprintf("Calling send_default_irob with %d bytes\n", total_bytes);
    rc = send_default_irob(next_irob, csock, data, total_bytes, 
                           send_labels, resume_handler, rh_arg);

    TIME(end);
    TIMEDIFF(begin, end, diff);
    dbgprintf("Completed request in %lu.%06lu seconds (default_irob_writev)\n",
	      diff.tv_sec, diff.tv_usec);

    return rc;
}

int
CMMSocketImpl::validate_default_irob(u_long send_labels,
                                     resume_handler_t resume_handler, void *rh_arg,
                                     CSocket *& csock)
{
    if (is_shutting_down()) {
	dbgprintf("Tried to send default IROB, but mc_socket %d is shutting down\n", 
		  sock);
	errno = EPIPE;
	return CMM_FAILED;
    }

    // checking for thunking here makes sense too; it's separate
    //  from the begin->chunk->ehd function flow.
    int ret = get_csock(send_labels, resume_handler, rh_arg, csock, true);
    if (ret < 0) {
        return ret;
    }
    assert(csock);

    return 0;
}

int
CMMSocketImpl::send_default_irob(irob_id_t id, CSocket *csock,
                                 char *buf, size_t len,
                                 u_long send_labels,
                                 resume_handler_t resume_handler, void *rh_arg)
{
    {
        PendingIROB *pirob = new PendingSenderIROB(id, 0, NULL, len, buf,
                                                   send_labels, 
                                                   resume_handler, rh_arg);
        
        PthreadScopedLock lock(&scheduling_state_lock);
        bool success = outgoing_irobs.insert(pirob);
        assert(success);

        if (csock->is_connected()) {
            csock->irob_indexes.new_irobs.insert(IROBSchedulingData(id, false, send_labels));
        } else {
            irob_indexes.new_irobs.insert(IROBSchedulingData(id, false, send_labels));
        }
        // the CSocketSender will insert the chunk and end_irob

        pthread_cond_broadcast(&scheduling_state_cv);
    }
    
    return len;
}

int
CMMSocketImpl::irob_relabel(irob_id_t id, u_long new_labels)
{
    /* TODO: 
     * for each index_set in (mine and my csocks'):
     *     index_set->transfer(id, new_labels, this->irob_indexes);
     * pthread_cond_broadcast(&sk->scheduling_state_cv);
     */
    return 0;
}

void
CMMSocketImpl::ack_received(irob_id_t id)
{
    PthreadScopedLock lock(&scheduling_state_lock);
    ack_timeouts.remove(id);
    PendingIROB *pirob = outgoing_irobs.find(id);
    if (!pirob) {
        if (outgoing_irobs.past_irob_exists(id)) {
            dbgprintf("Duplicate ack received for IROB %ld; ignoring\n",
                      id);
            return;
        } else {
            dbgprintf("Ack received for non-existent IROB %ld\n", id);
            throw CMMException();
        }
    }

    PendingSenderIROB *psirob = dynamic_cast<PendingSenderIROB*>(pirob);
    assert(psirob);

    psirob->ack();
    remove_if_unneeded(pirob);
    dbgprintf("Ack received for IROB %ld; %d unACK'd IROBs remain\n", 
              id, outgoing_irobs.size());
}

void
CMMSocketImpl::resend_request_received(irob_id_t id, resend_request_type_t request,
                                       u_long seqno, int next_chunk)//, size_t offset, size_t len)
{
    {
        // try to make sure there's a socket to do the resending
        PthreadScopedRWLock sock_lock(&my_lock, false);
        CSocket *csock = NULL;
        int ret = get_csock(0, NULL, NULL, csock, false);
        if (ret < 0) {
            goodbye(false);
            return;
        }
    }

    PthreadScopedLock lock(&scheduling_state_lock);
    PendingIROB *pirob = outgoing_irobs.find(id);
    if (!pirob) {
        dbgprintf("Resend request received for non-existent IROB %ld\n", id);
        throw CMMException();
    }
    PendingSenderIROB *psirob = dynamic_cast<PendingSenderIROB*>(pirob);
    assert(psirob);
    u_long send_labels = psirob->send_labels;

    if (request & CMM_RESEND_REQUEST_DEPS) {
        dbgprintf("Enqueuing resend of deps for IROB %ld\n", id);
        irob_indexes.new_irobs.insert(IROBSchedulingData(id, false, send_labels));
    }
    if (request & CMM_RESEND_REQUEST_DATA) {
        dbgprintf("Enqueuing resend of chunk %lu for IROB %ld\n", seqno, id);
        psirob->mark_not_received(seqno);//, offset, len);
        irob_indexes.new_chunks.insert(IROBSchedulingData(id, true, send_labels));
    }
    if (request & CMM_RESEND_REQUEST_END) {
        if (psirob->is_complete() && psirob->all_chunks_sent()) {
            dbgprintf("Enqueuing resend of End_IROB for IROB %ld\n", id);
            irob_indexes.finished_irobs.insert(IROBSchedulingData(id, false, send_labels));
        }
        dbgprintf("Enqueuing resend of chunks %d-%zu for IROB %ld\n", 
                  next_chunk, psirob->sent_chunks.size(), id);
        psirob->mark_drop_point(next_chunk);
        irob_indexes.new_chunks.insert(IROBSchedulingData(id, true, send_labels));
    }
    psirob->data_check = false;
    pthread_cond_broadcast(&scheduling_state_cv);
}

void
CMMSocketImpl::data_check_requested(irob_id_t id)
{
    {
        // try to make sure there's a socket to do the resending
        PthreadScopedRWLock sock_lock(&my_lock, false);
        CSocket *csock = NULL;
        int ret = get_csock(0, NULL, NULL, csock, false);
        if (ret < 0) {
            goodbye(false);
            return;
        }
    }

    PthreadScopedLock lock(&scheduling_state_lock);
    PendingIROB *pirob = incoming_irobs.find(id);
    if (!pirob) {
        if (incoming_irobs.past_irob_exists(id)) {
            // already received, passed to application; just (re)send the ACK
            
            // service time is probably large (invalid)
            //  if the data flow has been interrupted,
            //  just tell the other end to ignore this IROB for
            //  measurement purposes
            struct timeval inval = {0, -1};
            IROBSchedulingData data(id, inval);
            irob_indexes.waiting_acks.insert(data);
            pthread_cond_broadcast(&scheduling_state_cv);
        } else {
            // never seen this one before; ask for everything
            IROBSchedulingData request(id, CMM_RESEND_REQUEST_ALL);
            irob_indexes.resend_requests.insert(request);
            pthread_cond_broadcast(&scheduling_state_cv);
        }
    } else {
        resend_request_type_t reqtype = CMM_RESEND_REQUEST_NONE;
        if (pirob->placeholder) {
            reqtype = resend_request_type_t(reqtype |
                                            CMM_RESEND_REQUEST_DEPS);
        }
        PendingReceiverIROB *prirob = dynamic_cast<PendingReceiverIROB*>(pirob);
        assert(prirob);
        if (!prirob->get_missing_chunks().empty()) {
            reqtype = resend_request_type_t(reqtype
                                            | CMM_RESEND_REQUEST_DATA);
        }
        if (prirob->expected_bytes == -1) {
            reqtype = resend_request_type_t(reqtype
                                            | CMM_RESEND_REQUEST_END);
        }

        if (reqtype == CMM_RESEND_REQUEST_NONE) {
            struct timeval inval = {0, -1};
            IROBSchedulingData data(id, inval);
            irob_indexes.waiting_acks.insert(data);
        } else {
            IROBSchedulingData request(id, reqtype);
            irob_indexes.resend_requests.insert(request);
        }
        pthread_cond_broadcast(&scheduling_state_cv);
    }
}

/* call only with scheduling_state_lock held */
void CMMSocketImpl::remove_if_unneeded(PendingIROB *pirob)
{
    assert(pirob);
    PendingSenderIROB *psirob = dynamic_cast<PendingSenderIROB*>(pirob);
    assert(psirob);
    if (psirob->is_acked() && psirob->is_complete()) {
        outgoing_irobs.erase(pirob->id);
        delete pirob;

	if (outgoing_irobs.empty()) {
	    if (shutting_down) {
                // make sure a sender thread wakes up 
                // to send the goodbye-ack
                pthread_cond_broadcast(&scheduling_state_cv);
	    }
	}
    }
}

void
CMMSocketImpl::goodbye(bool remote_initiated)
{
    PthreadScopedLock lock(&scheduling_state_lock);
    if (shutting_down) {
        //csock_map->join_to_all_workers();

	// make sure recv()s get woken up if they need to fail
	pthread_cond_broadcast(&scheduling_state_cv);
	return;
    }

    shutting_down = true; // picked up by sender-scheduler
    if (remote_initiated) {
        remote_shutdown = true;
    }

    CSocket *csock = NULL;
    int ret = -1;
    if (remote_listener_port > 0) {
        /* that is, if this multisocket was bootstrapped */
        ret = get_csock(0, NULL, NULL, csock, false);
    }
    if (ret < 0) {
        // no socket to send the goodbye; connection must be gone
        remote_shutdown = true;
        goodbye_sent = true;

        //csock_map->join_to_all_workers();
        //return;
    }

    pthread_cond_broadcast(&scheduling_state_cv);

    // sender-scheduler thread will send goodbye msg after 
    // all ACKs are received
}

void 
CMMSocketImpl::goodbye_acked(void)
{
    PthreadScopedLock lock(&scheduling_state_lock);
    assert(shutting_down);
    remote_shutdown = true;
    pthread_cond_broadcast(&scheduling_state_cv);
}

void
CMMSocketImpl::prepare_app_operation()
{
    pthread_t self = pthread_self();
    struct AppThread& thread = app_threads[self];
    
    pthread_mutex_lock(&thread.mutex);
    thread.rc = CMM_INVALID_RC;
    pthread_mutex_unlock(&thread.mutex);
}

/* returns result of pending operation, or -1 on error */
long 
CMMSocketImpl::wait_for_completion(u_long label)
{
    long rc;
    pthread_t self = pthread_self();
    struct AppThread& thread = app_threads[self];
    
    struct timespec abs_timeout;
    struct timespec *ptimeout = NULL;
    struct timespec relative_timeout = {-1, 0};
    if (failure_timeouts.find(label) != failure_timeouts.end()) {
        relative_timeout = failure_timeouts[label];
    }
    if (relative_timeout.tv_sec >= 0) {
        abs_timeout = abs_time(relative_timeout);
        ptimeout = &abs_timeout;
    }

    pthread_rwlock_unlock(&my_lock);
    
    PthreadScopedLock lock(&thread.mutex);
    while (thread.rc == CMM_INVALID_RC) {
        if (ptimeout) {
            rc = pthread_cond_timedwait(&thread.cv, &thread.mutex, ptimeout);
        } else {
            rc = pthread_cond_wait(&thread.cv, &thread.mutex);
        }
        pthread_rwlock_rdlock(&my_lock); // XXX: double-check!

        if (rc == ETIMEDOUT) {
            errno = rc;
            return CMM_FAILED;
        }
    }
    rc = thread.rc;
    
    return rc;
}

void 
CMMSocketImpl::signal_completion(pthread_t requester_tid, long rc)
{
    if (requester_tid != 0 &&
        app_threads.find(requester_tid) != app_threads.end()) {
        
        struct AppThread& thread = app_threads[requester_tid];
        
        pthread_mutex_lock(&thread.mutex);
        thread.rc = rc;
        
        // since there's one cv per app thread, we don't need to
        // broadcast here; at most one thread is waiting
        pthread_cond_signal(&thread.cv);
        pthread_mutex_unlock(&thread.mutex);
    }
}

CMMSocketImpl::static_destroyer::~static_destroyer()
{
    dbgprintf("Application is exiting\n");
    CMMSocketImpl::cleanup();
    dbgprintf("Waiting for internal threads to finish\n");
    CMMThread::join_all();
}

CMMSocketImpl::static_destroyer CMMSocketImpl::destroyer;

void
CMMSocketImpl::cleanup()
{
    dbgprintf("Cleaning up leftover mc_sockets\n");

    typedef map<mc_socket_t, CMMSocketImplPtr> TmpMap;
    TmpMap leftover_sockets;

    {
        PthreadScopedLock lock(&hashmaps_mutex);
        leftover_sockets.insert(cmm_sock_hash.begin(), cmm_sock_hash.end());
    }

    for (TmpMap::iterator sk_iter = leftover_sockets.begin();
         sk_iter != leftover_sockets.end(); sk_iter++) {
        CMMSocketImplPtr sk = sk_iter->second;
        PthreadScopedRWLock sock_lock(&sk->my_lock, false);
        sk->goodbye(false);
        shutdown(sk->select_pipe[0], SHUT_RDWR);
        shutdown(sk->select_pipe[1], SHUT_RDWR);
        
        PthreadScopedLock sch_lock(&sk->scheduling_state_lock);
        while (!sk->remote_shutdown || !sk->goodbye_sent) {
            pthread_cond_wait(&sk->scheduling_state_cv,
                              &sk->scheduling_state_lock);
        }
    }
}


int 
CMMSocketImpl::mc_get_failure_timeout(u_long label, struct timespec *ts)
{
    PthreadScopedRWLock sock_lock(&my_lock, false);

    if (ts) {
        struct timespec timeout = {-1, 0};
        if (failure_timeouts.find(label) != failure_timeouts.end()) {
            timeout = failure_timeouts[label];
        }
        *ts = timeout;
        return 0;
    } else {
        errno = EINVAL;
        return -1;
    }
}

int 
CMMSocketImpl::mc_set_failure_timeout(u_long label, const struct timespec *ts)
{
    PthreadScopedRWLock sock_lock(&my_lock, true);

    if (ts) {
        failure_timeouts[label] = *ts;
        return 0;
    } else {
        errno = EINVAL;
        return -1;
    }
}

// bool
// CMMSocketImpl::okay_to_send_bg(struct timeval& time_since_last_fg)
// {
//     struct timeval now;
//     TIME(now);
    
//     TIMEDIFF(last_fg, now, time_since_last_fg);
//     struct timeval wait_time = bg_wait_time();
//     return timercmp(&time_since_last_fg, &wait_time, >=);
// }

// struct timeval
// CMMSocketImpl::bg_wait_time()
// {
//     struct timeval avg = {0, 500000};

//     // wait 2x the avg time between FG requests.

//     // XXX: may want to tweak this to bound probability of 
//     //  interfering with foreground traffic.  That would
//     //  assume the foreground traffic pattern distribution
//     //  is consistent.
//     double count = (double)fg_count / 2;
//     if (fg_count > 0) {
//         timerdiv(&total_inter_fg_time, count, &avg);
//     }
//     return avg;
// }

void
CMMSocketImpl::update_last_fg()
{
    TIME(last_fg);
}
