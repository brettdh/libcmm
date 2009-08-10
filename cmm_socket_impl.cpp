#include "cmm_socket.h"
#include "cmm_socket.private.h"
#include "cmm_socket_sender.h"
#include "cmm_socket_receiver.h"
#include "cmm_internal_listener.h"
#include "csocket.h"
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

#include "debug.h"

#include <map>
#include <vector>
#include <set>
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
	sendr = new CMMSocketSender(this);
	recvr = new CMMSocketReceiver(this);
	listener_thread = new ListenerThread(this);
	
	sendr->start();
	recvr->start();
	listener_thread->start();
    
        pthread_mutex_lock(&hashmaps_mutex);
        for (NetInterfaceSet::iterator it = ifaces.begin();
             it != ifaces.end(); it++) {
            
            struct net_interface listener_addr;
            memset(&listener_addr, 0, sizeof(listener_addr));
            listener_addr.ip_addr = it->ip_addr;
            listener_addr.labels = it->labels;


            local_ifaces.insert(listener_addr);
        }
        pthread_mutex_unlock(&hashmaps_mutex);
        
        if (bootstrap_sock != -1) {
            /* we are accepting a connection */
            recv_remote_listeners(bootstrap_sock);
            send_local_listeners(bootstrap_sock);
        } else {
            /* we are connecting */
            assert(remote_addr);

            bootstrap_sock = socket(PF_INET, SOCK_STREAM, 0);
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
    CMMSocketImplPtr new_sk;
    mc_socket_t new_sock = -1;
    try {
	/* automatically clean up if cmm_sock() throws */
        CMMSocketImplPtr tmp(new CMMSocketImpl(family, type, protocol));
	new_sock = tmp->sock;
        new_sk = tmp;
    } catch (int oserr) {
	return oserr;
    }

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
    pthread_mutex_lock(&hashmaps_mutex);
    CMMSockHash::accessor ac;
    if (cmm_sock_hash.find(ac, sock)) {
	CMMSocketImplPtr sk(ac->second);
	sk->sendr->goodbye(false);
	cmm_sock_hash.erase(ac);
        /* the CMMSocket object gets destroyed by the shared_ptr. */
        /* moved the rest of the cleanup to the destructor */
        pthread_mutex_unlock(&hashmaps_mutex);
	return 0;
    } else {
        pthread_mutex_unlock(&hashmaps_mutex);
	fprintf(stderr, "Warning: cmm_close()ing a socket that's not "
		"in my hash\n");
	errno = EBADF;
	return -1;
    }
}


CMMSocketImpl::CMMSocketImpl(int family, int type, int protocol)
    : listener_thread(NULL), sendr(NULL), recvr(NULL),
      next_irob(0)
{
    /* reserve a dummy OS file descriptor for this mc_socket. */
    sock = socket(family, type, protocol);
    if (sock < 0) {
	/* invalid params, or no more FDs/memory left. */
	throw sock; /* :-) */
    }

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

    csock_map = new CSockMapping(this);
}

CMMSocketImpl::~CMMSocketImpl()
{
    delete csock_map;

    delete listener_thread;
    delete sendr;
    delete recvr;
    
    //free(remote_addr);
    close(sock);
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

    CMMSockHash::accessor ac;
    if (!cmm_sock_hash.find(ac, sock)) {
	/* already checked this above */
	assert(0);
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

/* assume the fds in mc_fds are mc_socket_t's.  
 * add the real osfds to os_fds, and
 * also put them in osfd_list, so we can iterate through them. 
 * maxosfd gets the largest osfd seen. */
int 
CMMSocketImpl::make_real_fd_set(int nfds, fd_set *fds,
				mcSocketOsfdPairList &osfd_list, 
				int *maxosfd)
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
	    sk->csock_map->get_real_fds(osfd_list);
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

    if (readfds) {
	tmp_readfds = *readfds;
	rc = make_real_fd_set(nfds, &tmp_readfds, readosfd_list, &maxosfd);
        if (rc < 0) {
            return -1;
        }
    }
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
    rc -= make_mc_fd_set(&tmp_readfds, readosfd_list);
    rc -= make_mc_fd_set(&tmp_writefds, writeosfd_list);
    rc -= make_mc_fd_set(&tmp_exceptfds, exceptosfd_list);

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
		       u_long send_labels, u_long recv_labels,
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
        lock(write_ac);
        id = next_irob++;
    }
    CMMSockHash::const_accessor read_ac;
    lock(read_ac);
    if (!sendr) {
        errno = ENOTCONN;
        return CMM_FAILED;
    }
    int rc = sendr->default_irob(id, buf, len, flags,
				 send_labels, recv_labels, 
				 resume_handler, arg);

    TIME(end);
    TIMEDIFF(begin, end, diff);
    dbgprintf("mc_send (%d bytes) took %lu.%06lu seconds, start-to-finish\n", 
	      rc, diff.tv_sec, diff.tv_usec);

    return rc;
}

int 
CMMSocketImpl::mc_writev(const struct iovec *vec, int count,
			 u_long send_labels, u_long recv_labels,
                         resume_handler_t resume_handler, void *arg)
{
    int rc;
    
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

    irob_id_t id = mc_begin_irob(-1, NULL, send_labels, recv_labels, 
                                 resume_handler, arg);
    if (id < 0) {
        return id;
    }
    
    int bytes = mc_irob_writev(id, vec, count);
    if (bytes != total_bytes) {
        return bytes;
    }

    rc = mc_end_irob(id);
    if (rc < 0) {
        return rc;
    }
    return bytes;
}

struct shutdown_each {
    int how;
    shutdown_each(int how_) : how(how_) {}

    int operator()(CSocket *csock) {
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
	rc = csock_map->for_each(shutdown_each(how));
	sendr->goodbye(false);
    } else {
	errno = EBADF;
	rc = -1;
    }

    return rc;
}

irob_id_t 
CMMSocketImpl::mc_begin_irob(int numdeps, const irob_id_t *deps, 
                             u_long send_labels, u_long recv_labels,
                             resume_handler_t rh, void *rh_arg)
{
    irob_id_t id = -1;
    {
        CMMSockHash::accessor write_ac;
        lock(write_ac);
        id = next_irob++;
    }
    CMMSockHash::const_accessor read_ac;
    lock(read_ac);
    if (!sendr) {
        errno = ENOTCONN;
        return CMM_FAILED;
    }
    int rc = sendr->begin_irob(id, numdeps, deps, 
                               send_labels, recv_labels,
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
    lock(read_ac);
    if (!sendr) {
        errno = ENOTCONN;
        return CMM_FAILED;
    }
    int rc = sendr->end_irob(id);
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
    lock(read_ac);
    if (!sendr) {
        errno = ENOTCONN;
        return CMM_FAILED;
    }
    return sendr->irob_chunk(id, buf, len, flags);
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
	CMMSockHash::const_accessor read_ac;
	if (!cmm_sock_hash.find(read_ac, sk_iter->first)) {
            assert(0);
	}
	CMMSocketImplPtr sk = read_ac->second;
	assert(sk);
        read_ac.release();

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
    int rc = listen(listener_sock, backlog);
    if (rc < 0) {
        return rc;
    }
    pthread_mutex_lock(&hashmaps_mutex);
    {
        VanillaListenerSet::const_accessor ac;
        (void)cmm_listeners.insert(ac, listener_sock);
    }
    pthread_mutex_unlock(&hashmaps_mutex);
    return 0;
}

mc_socket_t 
CMMSocketImpl::mc_accept(int listener_sock, 
                         struct sockaddr *addr, socklen_t *addrlen)
{
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
    lock(ac);

    if (!recvr) {
        errno = ENOTCONN;
        return CMM_FAILED;
    }

    struct timeval begin, end, diff;
    TIME(begin);
    int rc = recvr->recv(buf, count, 0, recv_labels);
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

    CSocket *csock = csock_map->csock_with_labels(0, 0);
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

    int operator()(CSocket *csock) {
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

    CSocket *csock = csock_map->csock_with_labels(0,0);
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
        if (local_ifaces.count(iface) > 0) {
            // make sure labels update if needed
            local_ifaces.erase(iface);
            changed_local_ifaces.erase(iface);
        }
        local_ifaces.insert(iface);
        changed_local_ifaces.insert(iface);
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
    CMMSockHash::accessor read_ac;
    lock(read_ac);
    
    csock_map->teardown(iface, local);

    if (local) {
        local_ifaces.erase(iface);
        changed_local_ifaces.insert(iface);
    } else {
        remote_ifaces.erase(iface);
    }
}

/* only called with read accessor held on this */
bool
CMMSocketImpl::net_available(u_long send_labels, u_long recv_labels)
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
    for (NetInterfaceSet::const_iterator it = remote_ifaces.begin();
         it != remote_ifaces.end(); it++) { 
        if (recv_labels == 0 || it->labels & recv_labels) {
            return true;
        }
    }
    return false;
}

bool 
CMMSocketImpl::net_available(mc_socket_t sock, 
                             u_long send_labels, u_long recv_labels)
{
    CMMSockHash::const_accessor read_ac;
    if (!cmm_sock_hash.find(read_ac, sock)) {
        return false;
    }
    CMMSocketImplPtr sk = read_ac->second;;
    return sk->net_available(send_labels, recv_labels);
}

/* grab a readlock on this socket with the accessor. */
void 
CMMSocketImpl::lock(CMMSockHash::const_accessor& ac)
{
    dbgprintf("Begin: read-lock msocket %d\n", sock);
    if (!cmm_sock_hash.find(ac, sock)) {
        assert(0);
    }
    dbgprintf("End: read-lock msocket %d\n", sock);
    assert(get_pointer(ac->second) == this);
}


/* grab a writelock on this socket with the accessor. */
void 
CMMSocketImpl::lock(CMMSockHash::accessor& ac)
{
    dbgprintf("Begin: write-lock msocket %d\n", sock);
    if (!cmm_sock_hash.find(ac, sock)) {
        assert(0);
    }
    dbgprintf("End: write-lock msocket %d\n", sock);
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
    CMMSocketSender *sendr;
    struct CMMSocketRequest req;

    BlockingRequest(CMMSocketSender *sendr_, struct CMMSocketRequest req_)
        : sendr(sendr_), req(req_) {}
};


static void unblock_thread(BlockingRequest *breq)
{
    // TODO: finish this up, using signal_completion to
    // wake up the blocking app request.
}

/* This function blocks until the data has been sent.
 * If the socket is non-blocking, we need to implement that here.
 */
int
CMMSocketImpl::begin_irob(irob_id_t next_irob, 
                            int numdeps, const irob_id_t *deps,
                            u_long send_labels, u_long recv_labels,
                            resume_handler_t resume_handler, void *rh_arg)
{
    if (is_shutting_down()) {
	dbgprintf("Tried to begin IROB, but mc_socket %d is shutting down\n", 
		  sk->sock);
	errno = EPIPE;
	return CMM_FAILED;
    }

    struct timeval begin, end, diff;
    TIME(begin);

    irob_id_t id = next_irob;

    CSocket *csock = NULL;
    
    try {
        csock = csock_map->new_csock_with_labels(send_labels, recv_labels);
    } catch (std::runtime_error& e) {
        dbgprintf("Error finding csocket by labels: %s\n", e.what());
        return CMM_FAILED;
    }

    if (!csock) {
        if (resume_handler) {
            enqueue_handler(sock, send_labels, recv_labels, 
                            resume_handler, arg);
            return CMM_DEFERRED;
        } else {
            // pseudo-thunk to block this until it's ready to send
            begin_app_operation();
            enqueue_handler(sock, send_labels, recv_labels,
                            unblock_thread, (void*)pthread_self());
        }
    }

    PendingSenderIROB *pirob = new PendingSenderIROB(id, numdeps, deps,
                                                     send_labels, recv_labels,
                                                     resume_handler, rh_arg);
    pirob->waiting_thread = pthread_self();

    {
        PendingIROBLattice::scoped_lock lock(outgoing_irobs);
        bool success = outgoing_irobs.insert(pirob);
        assert(success);
    }
    
    long rc = wait_for_completion();
    if (rc < 0) {
        PendingIROBLattice::scoped_lock lock(outgoing_irobs);
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
		  sk->sock);
	errno = EPIPE;
	return CMM_FAILED;
    }

    struct timeval begin, end, diff;
    TIME(begin);

    {
        PendingIROBLattice::scoped_lock lock(outgoing_irobs);

        PendingIROB *pirob = outgoing_irobs.find(id);
        if (!pirob) {
            return -1;
        }
        
        if (pirob->is_complete()) {
            dbgprintf("Trying to complete IROB %lu, "
                      "which is already complete\n", id);
            return -1;
        }
        pirob->finish();

        assert(pirob->waiting_thread == 0);
        pirob->waiting_thread = pthread_self();
    }
    
    long rc = wait_for_completion();
    if (rc != 0) {
        dbgprintf("end irob %d failed entirely; connection must be gone\n", id);
        return -1;
    } else {
        PendingIROBLattice::scoped_lock lock(outgoing_irobs);
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
		  sk->sock);
	errno = EPIPE;
	return CMM_FAILED;
    }

    struct timeval begin, end, diff;
    TIME(begin);

    struct irob_chunk_data chunk;
    {
        PendingIROBLattice::scoped_lock lock(outgoing_irobs);

        PendingIROB *pirob = outgoing_irobs.find(id);
        if (!pirob) {
	    dbgprintf("Tried to add to nonexistent IROB %d\n", id);
	    throw CMMException();
	}
	
	if (pirob->is_complete()) {
	    dbgprintf("Tried to add to complete IROB %d\n", id);
	    throw CMMException();
	}
	chunk.id = id;
	chunk.seqno = INVALID_IROB_SEQNO; /* will be overwritten 
					   * with valid seqno */
	chunk.datalen = len;
	chunk.data = new char[len];
	memcpy(chunk.data, buf, len);
	PendingSenderIROB *psirob = dynamic_cast<PendingSenderIROB*>(pirob);
	assert(psirob);
	psirob->add_chunk(chunk); /* writes correct seqno into struct */
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
			      u_long send_labels, u_long recv_labels,
			      resume_handler_t resume_handler, void *rh_arg)
{
    if (is_shutting_down()) {
	dbgprintf("Tried to send default IROB, but mc_socket %d is shutting down\n", 
		  sk->sock);
	errno = EPIPE;
	return CMM_FAILED;
    }

    struct timeval begin, end, diff;
    TIME(begin);

    irob_id_t id = next_irob;

    char *data = new char[len];
    memcpy(data, buf, len);
    {
        PendingIROB *pirob = new PendingSenderIROB(id, len, data,
						   send_labels, recv_labels,
                                                   resume_handler, rh_arg);
        pirob->waiting_thread = pthread_self();

        PendingIROBLattice::scoped_lock lock(outgoing_irobs);
        bool success = outgoing_irobs.insert(pirob);
        assert(success);
    }
    
    long rc = wait_for_completion();
    TIME(end);
    TIMEDIFF(begin, end, diff);
    dbgprintf("Completed request in %lu.%06lu seconds (default_irob)\n",
	      diff.tv_sec, diff.tv_usec);

    return rc;
}

void
CMMSocketImpl::ack_received(irob_id_t id, u_long seqno)
{
    PendingIROBLattice::scoped_lock lock(outgoing_irobs);
    PendingIROB *pirob = outgoing_irobs.find(id);
    if (!pirob) {
        dbgprintf("Ack received for non-existent IROB %d\n", id);
        throw CMMException();
    }

    PendingSenderIROB *psirob = dynamic_cast<PendingSenderIROB*>(pirob);
    assert(psirob);

    psirob->ack(seqno);
    remove_if_unneeded(pirob);
}

/* call only with lock held on outgoing_irobs */
void CMMSocketImpl::remove_if_unneeded(PendingIROB *pirob)
{
    assert(pirob);
    PendingSenderIROB *psirob = dynamic_cast<PendingSenderIROB*>(pirob);
    assert(psirob);
    if (psirob->is_acked() && psirob->is_complete()) {
        outgoing_irobs.erase(pirob->id);
        delete pirob;
	ac.release();

        pthread_mutex_lock(&shutdown_mutex);
	if (outgoing_irobs.empty()) {
	    if (shutting_down) {
		pthread_cond_signal(&shutdown_cv);
	    }
	}
        pthread_mutex_unlock(&shutdown_mutex);
    }
}

void
CMMSocketImpl::goodbye(bool remote_initiated)
{
    if (is_shutting_down()) {
	return;
    }

    pthread_mutex_lock(&shutdown_mutex);
    shutting_down = true; // picked up by sender-scheduler
    if (remote_initiated) {
	remote_shutdown = true;
    }
    while (!outgoing_irobs.empty()) {
	pthread_cond_wait(&shutdown_cv, &shutdown_mutex);
    }

    // sender-scheduler thread will send goodbye msg after 
    // all ACKs are received

    while (!remote_shutdown) {
	pthread_cond_wait(&shutdown_cv, &shutdown_mutex);
    }
    pthread_mutex_unlock(&shutdown_mutex);
    sk->recvr->shutdown();
}

void 
CMMSocketImpl::goodbye_acked(void)
{
    pthread_mutex_lock(&shutdown_mutex);
    assert(shutting_down);
    remote_shutdown = true;
    pthread_cond_signal(&shutdown_cv);
    pthread_mutex_unlock(&shutdown_mutex);
}

void
CMMSocketImpl::begin_app_operation()
{
    pthread_t self = pthread_self();
    struct AppThread& thread = app_threads[self];

    pthread_mutex_lock(&thread.mutex);
    thread.rc = CMM_INVALID_RC;
    pthread_mutex_unlock(&thread_mutex);
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
