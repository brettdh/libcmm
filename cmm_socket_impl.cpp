#include "cmm_socket.h"
#include "cmm_socket.private.h"
#include "cmm_internal_listener.h"
#include "csocket.h"
#include "csocket_mapping.h"
#include "common.h"
#include "libcmm.h"
#include "libcmm_ipc.h"
#include <connmgr_labels.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>

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

CMMSockHash CMMSocketImpl::cmm_sock_hash;
VanillaListenerSet CMMSocketImpl::cmm_listeners;
NetInterfaceSet CMMSocketImpl::ifaces;
IROBSockHash CMMSocketImpl::irob_sock_hash;
pthread_mutex_t CMMSocketImpl::hashmaps_mutex = PTHREAD_MUTEX_INITIALIZER;

void CMMSocketImpl::recv_remote_listener(int bootstrap_sock)
{
    struct CMMSocketControlHdr hdr = {0};
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
    
    remote_ifaces.insert(new_listener);
    dbgprintf("Got new remote interface %s with labels %lu\n",
	      inet_ntoa(new_listener.ip_addr), new_listener.labels);
}

void CMMSocketImpl::recv_remote_listeners(int bootstrap_sock)
{
    struct CMMSocketControlHdr hdr = {0};
    int rc = recv(bootstrap_sock, &hdr, sizeof(hdr), 0);
    if (rc != sizeof(hdr) || ntohs(hdr.type) != CMM_CONTROL_MSG_HELLO) {
	perror("recv");
	dbgprintf("Error receiving remote listeners\n");
        throw -1;
    }

    remote_listener_port = hdr.op.hello.listen_port;

    int num_ifaces = ntohl(hdr.op.hello.num_ifaces);
    for (int i = 0; i < num_ifaces; i++) {
        recv_remote_listener(bootstrap_sock);
    }
}

void CMMSocketImpl::send_local_listener(int bootstrap_sock, 
                                        struct net_interface iface)
{
    struct CMMSocketControlHdr hdr = {0};
    hdr.type = htons(CMM_CONTROL_MSG_NEW_INTERFACE);
    hdr.op.new_interface.ip_addr = iface.ip_addr;
    hdr.op.new_interface.labels = htonl(iface.labels);
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
    struct CMMSocketControlHdr hdr = {0};
    hdr.type = htons(CMM_CONTROL_MSG_HELLO);

    assert(listener_thread);
    hdr.op.hello.listen_port = listener_thread->port();
    hdr.op.hello.num_ifaces = htonl(local_ifaces.size());

    int rc = send(bootstrap_sock, &hdr, sizeof(hdr), 0);
    if (rc != sizeof(hdr)) {
	perror("send");
	dbgprintf("Error sending local listeners\n");
        throw -1;
    }

    for (NetInterfaceSet::iterator it = local_ifaces.begin();
         it != local_ifaces.end(); it++) {
        send_local_listener(bootstrap_sock, *it);
    }
}

void 
CMMSocketImpl::add_connection(int sock, 
                              struct in_addr local_addr,
                              struct in_addr remote_addr)
{
    csock_map->add_connection(sock, local_addr, remote_addr);
}

int
CMMSocketImpl::connection_bootstrap(const struct sockaddr *remote_addr, 
                                    socklen_t addrlen, int bootstrap_sock)
{
    /* TODO: non-blocking considerations? */
    try {
	listener_thread = new ListenerThread(this);
	int rc = listener_thread->start();
        if (rc != 0) {
            throw rc;
        }
    
        PthreadScopedLock scoped_lock(&hashmaps_mutex);
        CMMSockHash::accessor ac;
        write_lock(ac);
        for (NetInterfaceSet::iterator it = ifaces.begin();
             it != ifaces.end(); it++) {
            
            struct net_interface listener_addr;
            memset(&listener_addr, 0, sizeof(listener_addr));
            listener_addr.ip_addr = it->ip_addr;
            listener_addr.labels = it->labels;
            
            local_ifaces.insert(listener_addr);
        }
        
        //CMMSockHash::accessor ac;
        //write_lock(ac);

        if (bootstrap_sock != -1) {
            /* we are accepting a connection */
            recv_remote_listeners(bootstrap_sock);
            send_local_listeners(bootstrap_sock);
        } else {
            /* we are connecting */
            assert(remote_addr);

            bootstrap_sock = socket(sock_family, sock_type, sock_protocol);
            if (bootstrap_sock < 0) {
                throw bootstrap_sock;
            }

            try {
                int rc = connect(bootstrap_sock, remote_addr, addrlen);
                if (rc < 0) {
		    perror("connect");
		    dbgprintf("Error connecting bootstrap socket\n");
                    throw rc;
                }
		
                send_local_listeners(bootstrap_sock);
                recv_remote_listeners(bootstrap_sock);
            } catch (int error_rc) {
                close(bootstrap_sock);
                throw;
            }
            close(bootstrap_sock);
        }
    } catch (int error_rc) {
        return error_rc;
    }

    return 0;
}

mc_socket_t
CMMSocketImpl::create(int family, int type, int protocol)
{
    if (family != PF_INET) {
        // TODO: remove this restriction and support 
        // more socket types.
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
    CMMSockHash::accessor ac;
    if (cmm_sock_hash.insert(ac, new_sock)) {
	ac->second = new_sk;
    } else {
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
    CMMSockHash::const_accessor read_ac;
    if (!cmm_sock_hash.find(read_ac, sock)) {
        return CMMSocketPtr(new CMMSocketPassThrough(sock));
    } else {
        int rc = sanity_check(sock);
        assert(rc == 0);
        return read_ac->second;
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
    CMMSockHash::accessor ac;
    if (cmm_sock_hash.find(ac, sock)) {
	CMMSocketImplPtr sk(ac->second);
	sk->goodbye(false);
        pthread_mutex_lock(&hashmaps_mutex);
	cmm_sock_hash.erase(ac);
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

static void
set_non_blocking(int fd)
{
    int flags = fcntl(fd, F_GETFL);
    (void)fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

CMMSocketImpl::CMMSocketImpl(int family, int type, int protocol)
    : listener_thread(NULL),
      shutting_down(false),
      remote_shutdown(false),
      goodbye_sent(false),
      incoming_irobs(this),
      sending_goodbye(false),
      next_irob(0)
{
    /* reserve a dummy OS file descriptor for this mc_socket. */
    sock = socket(family, type, protocol);
    if (sock < 0) {
	/* invalid params, or no more FDs/memory left. */
	throw sock; /* :-) */
    }

    int rc = pipe(select_pipe);
    if (rc < 0) { 
        close(sock);
        throw rc;
    }
    set_non_blocking(select_pipe[0]);
    set_non_blocking(select_pipe[1]);

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

    non_blocking=0;

    csock_map = NULL;
    //csock_map = new CSockMapping(this);
    pthread_mutex_init(&shutdown_mutex, NULL);
    pthread_cond_init(&shutdown_cv, NULL);
    pthread_mutex_init(&scheduling_state_lock, NULL);
    pthread_cond_init(&scheduling_state_cv, NULL);
}

CMMSocketImpl::~CMMSocketImpl()
{
    delete csock_map;

    listener_thread->stop();
    delete listener_thread;
    
    //free(remote_addr);
    close(sock);

    pthread_mutex_destroy(&shutdown_mutex);
    pthread_cond_destroy(&shutdown_cv);
    pthread_mutex_destroy(&scheduling_state_lock);
    pthread_cond_destroy(&scheduling_state_cv);
}

int 
CMMSocketImpl::mc_connect(const struct sockaddr *serv_addr, 
                          socklen_t addrlen)
{
    {    
	CMMSockHash::const_accessor ac;
	if (!cmm_sock_hash.find(ac, sock)) {
            assert(0);

	    fprintf(stderr, 
		    "Error: tried to cmm_connect socket %d "
		    "not created by cmm_socket\n", sock);
	    errno = EBADF;
	    return CMM_FAILED; /* assert(0)? */
	}
	if (!ac->second->remote_ifaces.empty()) {
	    fprintf(stderr, 
		    "Warning: tried to cmm_connect an "
		    "already-connected socket %d\n", sock);
	    errno = EISCONN;
	    return CMM_FAILED;
	}
    }

    if (!serv_addr) {
        errno = EINVAL;
        return CMM_FAILED;
    }

//     CMMSockHash::accessor ac;
//     if (!cmm_sock_hash.find(ac, sock)) {
// 	/* already checked this above */
// 	assert(0);
//     }
    
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
                                  bool reading)
{
    if (reading) {
        osfd_list.push_back(make_pair(sock, select_pipe[0]));
        if (incoming_irobs.data_is_ready()) {
            // select can return now, so make sure it does
            char c = 42; // value will be ignored
            int rc = write(select_pipe[1], &c, 1);
            assert(rc == 1);
        }
    } else {
        csock_map->get_real_fds(osfd_list);
    }
}

/* assume the fds in mc_fds are mc_socket_t's.  
 * add the real osfds to os_fds, and
 * also put them in osfd_list, so we can iterate through them. 
 * maxosfd gets the largest osfd seen. */
int 
CMMSocketImpl::make_real_fd_set(int nfds, fd_set *fds,
				mcSocketOsfdPairList &osfd_list, 
				int *maxosfd, bool reading = false)
{
    if (!fds) {
	return 0;
    }

    //fprintf(stderr, "DBG: about to check fd_set %p for mc_sockets\n", fds);
    for (mc_socket_t s = nfds - 1; s > 0; s--) {
        //fprintf(stderr, "DBG: checking fd %d\n", s);
	if (FD_ISSET(s, fds)) {
            //fprintf(stderr, "DBG: fd %d is set\n", s);
	    CMMSockHash::const_accessor ac;
	    if (!cmm_sock_hash.find(ac, s)) {
                /* This must be a real file descriptor, not a mc_socket. 
                 * No translation needed. */
                continue;
	    }

	    FD_CLR(s, fds);
	    CMMSocketImplPtr sk = ac->second;
	    assert(sk);
	    sk->get_fds_for_select(osfd_list, reading);
	    if (osfd_list.size() == 0) {
#if 0
		/* XXX: what about the nonblocking case? */
		/* XXX: what if the socket is connected, but currently
		 * lacks physical connections? */
		fprintf(stderr,
			"DBG: cmm_select on a disconnected socket\n");
		errno = EBADF;
		return -1;
#endif
	    }
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
            }
	}
    }

    return dups;
}

/* TODO: reimplement for multi-sockets by selecting on a special pipe 
 * that is written to when data is available to read. */
int 
CMMSocketImpl::mc_select(mc_socket_t nfds, 
			 fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
			 struct timeval *timeout)
{
    int maxosfd = nfds;
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

    fprintf(stderr, "libcmm: mc_select: making real fd_sets\n");

    if (writefds) {
	tmp_writefds = *writefds;
	rc = make_real_fd_set(nfds, &tmp_writefds, writeosfd_list, &maxosfd);
        if (rc < 0) {
            return -1;
        }
    }
    if (exceptfds) {
	tmp_exceptfds = *exceptfds;
	rc = make_real_fd_set(nfds, &tmp_exceptfds, exceptosfd_list,&maxosfd);
        if (rc < 0) {
          return -1;
        }
    }
    if (readfds) {
	tmp_readfds = *readfds;
	rc = make_real_fd_set(nfds, &tmp_readfds, readosfd_list, &maxosfd, true);
        if (rc < 0) {
            return -1;
        }
    }

    fprintf(stderr, "libcmm: about to call select()\n");

    unblock_select_signals();
    rc = select(maxosfd + 1, &tmp_readfds, &tmp_writefds, &tmp_exceptfds, 
		timeout);
    block_select_signals();

    fprintf(stderr, "libcmm: returned from select()\n");
    
    if (rc < 0) {
	/* select does not modify the fd_sets if failure occurs */
	return rc;
    }

    /* map osfds back to mc_sockets, and correct for duplicates */
    rc -= make_mc_fd_set(&tmp_writefds, writeosfd_list);
    rc -= make_mc_fd_set(&tmp_exceptfds, exceptosfd_list);
    int read_fd_count = make_mc_fd_set(&tmp_readfds, readosfd_list);
    rc -= read_fd_count;
    if (read_fd_count > 0) {
        for (int i = 0; i < nfds - 1; ++i) {
            if (FD_ISSET(i, &tmp_readfds)) {
                CMMSockHash::const_accessor ac;
                if (!cmm_sock_hash.find(ac, i)) {
                    /* This must be a real file descriptor, 
                     * not a mc_socket.  Skip it. */
                    continue;
                }
                
                CMMSocketImplPtr sk = ac->second;
                assert(sk);
                char junk;
                int ret = read(sk->select_pipe[0], &junk, 1);
                if (ret != 1) {
                    // someone else got this select; a read might block.
                    // but then again, multiple threads selecting on
                    // the same socket is iffy to begin with
                }
            }
        }
    }
    
    if (readfds)   { *readfds   = tmp_readfds;   }
    if (writefds)  { *writefds  = tmp_writefds;  }
    if (exceptfds) { *exceptfds = tmp_exceptfds; }

    return rc;
}

/* TODO: similar to select(), make sure poll gets updated with 
 * new fds as needed */
int 
CMMSocketImpl::mc_poll(struct pollfd fds[], nfds_t nfds, int timeout)
{
    /* maps osfds to pointers into the original fds array */
    map<int, struct pollfd*> osfds_to_pollfds;
    vector<struct pollfd> real_fds_list;
    for(nfds_t i=0; i<nfds; i++) {
	mcSocketOsfdPairList osfd_list;
	CMMSockHash::const_accessor ac;	
	if(!cmm_sock_hash.find(ac, fds[i].fd)) {
	    real_fds_list.push_back(fds[i]);
	    osfds_to_pollfds[fds[i].fd] = &fds[i];
	    continue; //this is a non mc_socket
	} else {
	    CMMSocketImplPtr sk = ac->second;
	    assert(sk);
	    sk->csock_map->get_real_fds(osfd_list);
	    if (osfd_list.size() == 0) {
		/* XXX: is this right? should we instead
		 * wait for connections to poll on? */
		errno = ENOTCONN;
		return -1;
	    }
	    for (size_t j = 0; j < osfd_list.size(); j++) {
		/* copy struct pollfd, overwrite fd */
		real_fds_list.push_back(fds[i]);
		real_fds_list.back().fd = osfd_list[j].second;
		osfds_to_pollfds[osfd_list[j].second] = &fds[i];
	    }
	}
    }

    nfds_t real_nfds = real_fds_list.size();
    struct pollfd *realfds = new struct pollfd[real_nfds];
    for (nfds_t i = 0; i < real_nfds; i++) {
	realfds[i] = real_fds_list[i];
    }

    int rc = poll(realfds, real_nfds, timeout);
    if (rc <= 0) {
	return rc;
    }

    //int lastfd = -1;
    set<int> orig_fds;
    for (nfds_t i = 0; i < real_nfds; i++) {
	struct pollfd *origfd = osfds_to_pollfds[realfds[i].fd];
	assert(origfd);
	CMMSockHash::const_accessor ac;	
	if(!cmm_sock_hash.find(ac, fds[i].fd)) {
	    origfd->revents = realfds[i].revents;
	} else {
            //CMMSocketImplPtr sk = ac->second;
	    //assert(sk);
	    //sk->poll_map_back(origfd, &realfds[i]);

            /* XXX: does this make sense? 
             * If an event happened on any of the underlying FDs, 
             * it happened on the multi-socket */
            origfd->revents |= realfds[i].revents;
	}
        if (orig_fds.find(origfd->fd) == orig_fds.end()) {
            orig_fds.insert(origfd->fd);
        } else {
            /* correct return value for duplicates */
            rc--;
        }
    }
    delete realfds;

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
#ifdef IMPORT_RULES
    /** Rules Part 3: Update labels if a better interface is available**/
    /* TODO-IMPL: move/reference this code as necessary */
    labels = set_superior_label(sock, labels);	
#endif

    struct timeval begin, end, diff;
    TIME(begin);
    
    irob_id_t id = -1;
    {
        CMMSockHash::accessor write_ac;
        write_lock(write_ac);
        id = next_irob++;
    }
    CMMSockHash::const_accessor read_ac;
    read_lock(read_ac);

    int rc = default_irob(id, buf, len, flags,
                          send_labels, 
                          resume_handler, arg);

    TIME(end);
    TIMEDIFF(begin, end, diff);
    dbgprintf("mc_send (%d bytes) took %lu.%06lu seconds, start-to-finish\n", 
	      rc, diff.tv_sec, diff.tv_usec);

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
#ifdef IMPORT_RULES
    /** Rules Part 3: Update labels if a better interface is available**/
    /* TODO-IMPL: move/reference this code as necessary */
    labels = set_superior_label(sock, labels);	
#endif

    struct timeval begin, end, diff;
    TIME(begin);
    
    irob_id_t id = -1;
    {
        CMMSockHash::accessor write_ac;
        write_lock(write_ac);
        id = next_irob++;
    }
    CMMSockHash::const_accessor read_ac;
    read_lock(read_ac);

    dbgprintf("Calling default_irob with %d bytes\n", total_bytes);
    int rc = default_irob_writev(id, vec, count, total_bytes,
                                 send_labels, 
                                 resume_handler, arg);

    TIME(end);
    TIMEDIFF(begin, end, diff);
    dbgprintf("mc_writev (%d bytes) took %lu.%06lu seconds, start-to-finish\n", 
	      rc, diff.tv_sec, diff.tv_usec);
    
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
    CMMSockHash::accessor ac;
    if (cmm_sock_hash.find(ac, sock)) {
	goodbye(false);
        rc = csock_map->for_each(shutdown_each(how));
    } else {
	errno = EBADF;
	rc = -1;
    }

    return rc;
}

irob_id_t 
CMMSocketImpl::mc_begin_irob(int numdeps, const irob_id_t *deps, 
                             u_long send_labels, 
                             resume_handler_t rh, void *rh_arg)
{
    irob_id_t id = -1;
    {
        CMMSockHash::accessor write_ac;
        write_lock(write_ac);
        id = next_irob++;
    }
    CMMSockHash::const_accessor read_ac;
    read_lock(read_ac);
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
    CMMSockHash::const_accessor read_ac;
    read_lock(read_ac);
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
    CMMSockHash::const_accessor read_ac;
    read_lock(read_ac);
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
    for (int i = 0; i < count; i++) {
        memcpy(buf + i, vec[i].iov_base, vec[i].iov_len);
    }

    long rc = mc_irob_send(id, buf, buflen, 0);
    delete [] buf;
    return rc;
}


void
CMMSocketImpl::interface_up(struct net_interface up_iface)
{
    pthread_mutex_lock(&hashmaps_mutex);
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
    pthread_mutex_lock(&hashmaps_mutex);
    ifaces.erase(down_iface);

    /* put down the sockets connected on now-unavailable networks. */
    for (CMMSockHash::iterator sk_iter = cmm_sock_hash.begin();
	 sk_iter != cmm_sock_hash.end(); sk_iter++) {
	CMMSockHash::const_accessor read_ac;
	if (!cmm_sock_hash.find(read_ac, sk_iter->first)) {
            assert(0);
	}
	CMMSocketImplPtr sk = read_ac->second;
	assert(sk);
        read_ac.release();

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
        return accept(listener_sock, addr, addrlen);
    }
    
    int sock = accept(listener_sock, addr, addrlen);
    if (sock < 0) {
        return sock;
    }
    ac.release();
    
    mc_socket_t mc_sock = CMMSocketImpl::create(PF_INET, SOCK_STREAM, 0);
    CMMSocketPtr sk = CMMSocketImpl::lookup(mc_sock);
    CMMSocketImpl *sk_impl = dynamic_cast<CMMSocketImpl*>(get_pointer(sk));
    assert(sk_impl);
    int rc = sk_impl->connection_bootstrap(addr, *addrlen, sock);
    close(sock);
        
    if (rc < 0) {
        mc_close(mc_sock);
        return rc;
    }
    return mc_sock;
}

int 
CMMSocketImpl::mc_read(void *buf, size_t count, u_long *recv_labels)
{
    CMMSockHash::const_accessor ac;
    read_lock(ac);

    struct timeval begin, end, diff;
    TIME(begin);
    int rc = incoming_irobs.recv(buf, count, 0, recv_labels);
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
    CMMSockHash::const_accessor ac;
    if (!cmm_sock_hash.find(ac, sock)) {
	//return getsockopt(sock, level, optname, optval, optlen);

        // see CMMSocketPassThrough
        assert(0);
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

int
CMMSocketImpl::mc_setsockopt(int level, int optname, 
                               const void *optval, socklen_t optlen)
{
    int rc = 0;
    CMMSockHash::accessor ac;
    if (!cmm_sock_hash.find(ac, sock)) {
        // see CMMSocketPassThrough
        assert(0);
    }

    if (optname == O_NONBLOCK) {
	non_blocking = true;
    }

    rc = csock_map->for_each(set_sock_opt(level, optname, optval, optlen));
    if (rc < 0) {
	return rc;
    }
    /* all succeeded */

    rc = setsockopt(sock, level, optname, optval, optlen);
    if (rc < 0) {
        fprintf(stderr, "warning: failed setting socket option on "
                "dummy socket\n");
    }

    /* inserts if not present */
    struct sockopt &opt = sockopts[level][optname];
    if (opt.optval) {
	free(opt.optval);
    }
    opt.optlen = optlen;
    opt.optval = malloc(optlen);
    assert(opt.optval);
    memcpy(opt.optval, optval, optlen);

    return 0;
}

int 
CMMSocketImpl::mc_getpeername(struct sockaddr *address, 
                              socklen_t *address_len)
{
    CMMSockHash::const_accessor ac;
    if (!cmm_sock_hash.find(ac, sock)) {
        // see CMMSocketPassThrough
        assert(0);

	/* pass-through for non-mc-sockets; now a layer above */
	// return getpeername(sock, address, address_len);
    }

    CSocketPtr csock = csock_map->csock_with_labels(0);
    if (!csock) {
        /* XXX: maybe instead create a connection and then proceed */
        errno = ENOTCONN;
        return -1;
    } 

    return getpeername(csock->osfd, address, address_len);
}

void
CMMSocketImpl::setup(struct net_interface iface, bool local)
{
    CMMSockHash::accessor write_ac;
    if (!cmm_sock_hash.find(write_ac, sock)) {
        assert(0);
    }
    assert(get_pointer(write_ac->second) == this);
    
    if (local) {
        PthreadScopedLock lock(&scheduling_state_lock);
        if (local_ifaces.count(iface) > 0) {
            // make sure labels update if needed
            local_ifaces.erase(iface);
            changed_local_ifaces.erase(iface);
        }
        local_ifaces.insert(iface);
        changed_local_ifaces.insert(iface);
        pthread_cond_broadcast(&scheduling_state_cv);
    } else {
        if (remote_ifaces.count(iface) > 0) {
            // make sure labels update if needed
            remote_ifaces.erase(iface);
        }
        remote_ifaces.insert(iface);
    }
}

void
CMMSocketImpl::teardown(struct net_interface iface, bool local)
{
    CMMSockHash::accessor ac;
    write_lock(ac);
    
    csock_map->teardown(iface, local);

    if (local) {
        PthreadScopedLock lock(&scheduling_state_lock);
        local_ifaces.erase(iface);
        changed_local_ifaces.insert(iface);
        pthread_cond_broadcast(&scheduling_state_cv);
    } else {
        remote_ifaces.erase(iface);
    }
}

/* only called with read accessor held on this */
bool
CMMSocketImpl::net_available(u_long send_labels)
{
    bool local_found = false;
    for (NetInterfaceSet::const_iterator it = local_ifaces.begin();
         it != local_ifaces.end(); it++) {
        if (send_labels == 0 || it->labels & send_labels) {
            local_found = true;
            break;
        }
    }
    if (!local_found) {
        return false;
    }
    if (!remote_ifaces.empty()) {
        return true;
    }

    return false;
}

bool 
CMMSocketImpl::net_available(mc_socket_t sock, 
                             u_long send_labels)
{
    CMMSockHash::const_accessor read_ac;
    if (!cmm_sock_hash.find(read_ac, sock)) {
        return false;
    }
    CMMSocketImplPtr sk = read_ac->second;;
    return sk->net_available(send_labels);
}

/* grab a readlock on this socket with the accessor. */
void 
CMMSocketImpl::read_lock(CMMSockHash::const_accessor& ac)
{
    //dbgprintf("Begin: read-lock msocket %d\n", sock);
    if (!cmm_sock_hash.find(ac, sock)) {
        assert(0);
    }
    //dbgprintf("End: read-lock msocket %d\n", sock);
    assert(get_pointer(ac->second) == this);
}


/* grab a writelock on this socket with the accessor. */
void 
CMMSocketImpl::write_lock(CMMSockHash::accessor& ac)
{
    //dbgprintf("Begin: write-lock msocket %d\n", sock);
    if (!cmm_sock_hash.find(ac, sock)) {
        assert(0);
    }
    //dbgprintf("End: write-lock msocket %d\n", sock);
    assert(get_pointer(ac->second) == this);
}

bool
CMMSocketImpl::is_shutting_down()
{
    pthread_mutex_lock(&shutdown_mutex);
    bool shdwn = shutting_down;
    pthread_mutex_unlock(&shutdown_mutex);
    return shdwn;
}

/* TODO: what about a timeout for these? Maybe just check if
 * SO_TIMEOUT is set for the socket. */
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

void 
CMMSocketImpl::wait_for_labels(u_long send_labels)
{
    // pseudo-thunk to block this until it's ready to send
    prepare_app_operation();
    enqueue_handler(sock, send_labels, 
                    (resume_handler_t)unblock_thread_thunk, 
                    new BlockingRequest(this, pthread_self()));
    (void)wait_for_completion();
    
}

int
CMMSocketImpl::get_csock(u_long send_labels, 
                         resume_handler_t resume_handler, void *rh_arg,
                         CSocket *& csock, bool blocking)
{
    try {
        csock = get_pointer(csock_map->new_csock_with_labels(send_labels));
        if (!csock) {
            if (resume_handler) {
                enqueue_handler(sock, send_labels, 
                                resume_handler, rh_arg);
                return CMM_DEFERRED;
            } else {
                if (blocking) {
                    while (!csock) {
                        wait_for_labels(send_labels);
                        csock = get_pointer(csock_map->new_csock_with_labels(send_labels));
                    }
                    return 0;
                } else {
                    return CMM_FAILED;
                }
            }
        } else {
            return 0;
        }
    } catch (std::runtime_error& e) {
        dbgprintf("Error finding csocket by labels: %s\n", e.what());
        return CMM_FAILED;
    }
}

/* This function blocks until the data has been sent.
 * If the socket is non-blocking, we need to implement that here.
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
                                                     send_labels, 
                                                     resume_handler, rh_arg);

    prepare_app_operation();

    {
        PthreadScopedLock lock(&scheduling_state_lock);
        bool success = outgoing_irobs.insert(pirob);
        assert(success);

        csock->irob_indexes.new_irobs.insert(IROBSchedulingData(id));
        pthread_cond_broadcast(&scheduling_state_cv);
    }
    
    long rc = wait_for_completion();
    if (rc < 0) {
        PthreadScopedLock lock(&scheduling_state_lock);
        outgoing_irobs.erase(id);
    }
    TIME(end);
    TIMEDIFF(begin, end, diff);
    dbgprintf("Completed request in %lu.%06lu seconds (begin_irob)\n",
	      diff.tv_sec, diff.tv_usec);
    
    return rc;
}

/* This function blocks until the data has been sent.
 * If the socket is non-blocking, we need to implement that here.
 */
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
            return -1;
        }
        
        if (pirob->is_complete()) {
            dbgprintf("Trying to complete IROB %lu, "
                      "which is already complete\n", id);
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

    prepare_app_operation();
    {
        PthreadScopedLock lock(&scheduling_state_lock);
        pirob->finish();
        
        PendingSenderIROB *psirob = dynamic_cast<PendingSenderIROB*>(pirob);
        assert(psirob);
        assert(psirob->waiting_thread == 0);
        psirob->waiting_thread = pthread_self();
        
        if (send_labels == 0) {
            irob_indexes.finished_irobs.insert(IROBSchedulingData(id));
        } else {
            csock->irob_indexes.finished_irobs.insert(IROBSchedulingData(id));
        }
        pthread_cond_broadcast(&scheduling_state_cv);
    }
    
    long rc = wait_for_completion();
    if (rc != 0) {
        dbgprintf("end irob %d failed entirely; connection must be gone\n", id);
        return -1;
    } else {
        PthreadScopedLock lock(&scheduling_state_lock);
        PendingIROB *pirob = outgoing_irobs.find(id);
        if (pirob) {
	    remove_if_unneeded(pirob);
	}
    }
    TIME(end);
    TIMEDIFF(begin, end, diff);
    dbgprintf("Completed request in %lu.%06lu seconds (end_irob)\n",
	      diff.tv_sec, diff.tv_usec);

    return rc;
}


/* This function blocks until the data has been sent.
 * If the socket is non-blocking, we need to implement that here.
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
	    dbgprintf("Tried to add to nonexistent IROB %d\n", id);
	    throw CMMException();
	}
	
	if (pirob->is_complete()) {
	    dbgprintf("Tried to add to complete IROB %d\n", id);
	    throw CMMException();
	}

        psirob = dynamic_cast<PendingSenderIROB*>(pirob);
        assert(psirob);
        send_labels = psirob->send_labels;
        resume_handler = psirob->resume_handler;
        rh_arg = psirob->rh_arg;
    }

    int ret = get_csock(send_labels,
                       resume_handler, rh_arg, csock, true);
    if (ret < 0) {
        return ret;
    }
    assert(csock);
    
    prepare_app_operation();
    {
        PthreadScopedLock lock(&scheduling_state_lock);

	chunk.id = id;
	chunk.seqno = INVALID_IROB_SEQNO; /* will be overwritten 
					   * with valid seqno */
	chunk.datalen = len;
	chunk.data = new char[len];
	memcpy(chunk.data, buf, len);

	psirob->add_chunk(chunk); /* writes correct seqno into struct */

        if (send_labels == 0) {
            // unlabeled send; let any thread pick it up
            irob_indexes.new_chunks.insert(IROBSchedulingData(id, chunk.seqno));
        } else {
            csock->irob_indexes.new_chunks.insert(IROBSchedulingData(id, chunk.seqno));
        }
        pthread_cond_broadcast(&scheduling_state_cv);
    }
    
    long rc = wait_for_completion();
    TIME(end);
    TIMEDIFF(begin, end, diff);
    dbgprintf("Completed request in %lu.%06lu seconds (irob_chunk)\n",
	      diff.tv_sec, diff.tv_usec);

    return rc;
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
    prepare_app_operation();
    {
        PendingIROB *pirob = new PendingSenderIROB(id, len, buf,
						   send_labels, 
                                                   resume_handler, rh_arg);

        PthreadScopedLock lock(&scheduling_state_lock);
        bool success = outgoing_irobs.insert(pirob);
        assert(success);

        if (send_labels == 0) {
            irob_indexes.new_irobs.insert(IROBSchedulingData(id));
        } else {
            csock->irob_indexes.new_irobs.insert(IROBSchedulingData(id));
        }
        pthread_cond_broadcast(&scheduling_state_cv);
    }
    
    long rc = wait_for_completion();

    return rc;
}

void
CMMSocketImpl::ack_received(irob_id_t id)
{
    PthreadScopedLock lock(&scheduling_state_lock);
    PendingIROB *pirob = outgoing_irobs.find(id);
    if (!pirob) {
        dbgprintf("Ack received for non-existent IROB %d\n", id);
        throw CMMException();
    }

    PendingSenderIROB *psirob = dynamic_cast<PendingSenderIROB*>(pirob);
    assert(psirob);

    psirob->ack();
    remove_if_unneeded(pirob);
}

/* call only with scheduling_state_lock held */
void CMMSocketImpl::remove_if_unneeded(PendingIROB *pirob)
{
    assert(pirob);
    PendingSenderIROB *psirob = dynamic_cast<PendingSenderIROB*>(pirob);
    assert(psirob);
    if (psirob->is_acked() && psirob->is_complete() &&
        psirob->waiting_thread == 0 &&
        psirob->waiting_threads.empty()) {
        outgoing_irobs.erase(pirob->id);
        delete pirob;

        pthread_mutex_lock(&shutdown_mutex);
	if (outgoing_irobs.empty()) {
	    if (shutting_down) {
                // make sure a sender thread wakes up 
                // to send the goodbye-ack
                pthread_cond_broadcast(&scheduling_state_cv);
	    }
	}
        pthread_mutex_unlock(&shutdown_mutex);
    }
}

void
CMMSocketImpl::goodbye(bool remote_initiated)
{
    if (is_shutting_down()) {
        csock_map->join_to_all_workers();
	return;
    }

    pthread_mutex_lock(&scheduling_state_lock);
    pthread_mutex_lock(&shutdown_mutex);
    shutting_down = true; // picked up by sender-scheduler
    if (remote_initiated) {
	remote_shutdown = true;
    }
    pthread_mutex_unlock(&shutdown_mutex);

    CSocket *csock;
    int ret = get_csock(0, NULL, NULL, csock, false);
    if (ret < 0) {
        // no socket to send the goodbye; connection must be gone
        pthread_mutex_lock(&shutdown_mutex);
        remote_shutdown = true;
        goodbye_sent = true;
        pthread_mutex_unlock(&shutdown_mutex);

        csock_map->join_to_all_workers();
        return;
    }

    pthread_cond_broadcast(&scheduling_state_cv);
    pthread_mutex_unlock(&scheduling_state_lock);

    // sender-scheduler thread will send goodbye msg after 
    // all ACKs are received

    pthread_mutex_lock(&shutdown_mutex);
    while (!remote_shutdown || !goodbye_sent) {
	pthread_cond_wait(&shutdown_cv, &shutdown_mutex);
    }
    pthread_mutex_unlock(&shutdown_mutex);
    //incoming_irobs.shutdown();

    if (!remote_initiated) {
        csock_map->join_to_all_workers();
    }
}

void 
CMMSocketImpl::goodbye_acked(void)
{
    pthread_mutex_lock(&shutdown_mutex);
    assert(shutting_down);
    remote_shutdown = true;
    pthread_cond_broadcast(&shutdown_cv);
    pthread_mutex_unlock(&shutdown_mutex);
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
CMMSocketImpl::wait_for_completion()
{
    long rc;
    pthread_t self = pthread_self();
    struct AppThread& thread = app_threads[self];

    pthread_mutex_lock(&thread.mutex);
    while (thread.rc == CMM_INVALID_RC) {
        pthread_cond_wait(&thread.cv, &thread.mutex);
    }
    rc = thread.rc;
    pthread_mutex_unlock(&thread.mutex);

    return rc;
}

void 
CMMSocketImpl::signal_completion(pthread_t requester_tid, long rc)
{
    if (requester_tid != 0) {
	assert(app_threads.find(requester_tid) != app_threads.end());
	struct AppThread& thread = app_threads[requester_tid];
	
	pthread_mutex_lock(&thread.mutex);
	thread.rc = rc;

	// since there's one cv per app thread, we don't need to
	// broadcast here; at most one thread is waiting
	pthread_cond_signal(&thread.cv);
	pthread_mutex_unlock(&thread.mutex);
    }
}
