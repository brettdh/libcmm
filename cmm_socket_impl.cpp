#include "cmm_socket.h"
#include "cmm_socket.private.h"
#include "csocket.h"
#include "libcmm.h"
#include "libcmm_ipc.h"
#include <connmgr_labels.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>

#include <signal.h>
#include "signals.h"
#include "thunks.h"
#include "cmm_timing.h"

#include <map>
#include <vector>
#include <set>
using std::map; using std::vector;
using std::set; using std::pair;

CMMSockHash CMMSocketImpl::cmm_sock_hash;
VanillaListenerSet CMMSocketImpl::cmm_listeners;
NetInterfaceMap CMMSocketImpl::ifaces;

void CMMSocketImpl::recv_remote_listener(int bootstrap_sock)
{
    struct CMMSocketControlHdr hdr;
    int rc = recv(bootstrap_sock, &hdr, sizeof(hdr), 0);
    if (rc != sizeof(hdr)) {
        throw -1;
    }
    if (hdr.type != CMM_CONTROL_MSG_NEW_INTERFACE) {
        throw -1;
    }
    struct ListenerAddr new_listener;
    memset(&new_listener.addr, 0, sizeof(new_listener.addr));
    new_listener.addr.sin_addr.s_addr = hdr.op.new_interface.ip_addr;
    new_listener.addr.sin_port = hdr.op.new_interface.port;
    new_listener.labels = hdr.op.new_interface.labels;
    remote_ifaces.push_back(new_listener);
}

void CMMSocketImpl::recv_remote_listeners(int bootstrap_sock)
{
    size_t num_ifaces = 0;
    int rc = recv(bootstrap_sock, &num_ifaces, sizeof(num_ifaces), 0);
    if (rc != sizeof(num_ifaces)) {
        throw -1;
    }

    num_ifaces = ntohl(num_ifaces);
    for (size_t i = 0; i < num_ifaces; i++) {
        recv_remote_listener(bootstrap_sock);
    }
}

void CMMSocketImpl::send_local_listener(int bootstrap_sock, 
                                        struct sockaddr_in addr)
{
    struct CMMSocketControlHdr hdr;
    hdr.type = htons(CMM_CONTROL_MSG_NEW_INTERFACE);
    hdr.op.new_interface.ip_addr = addr.sin_addr.s_addr;
    hdr.op.new_interface.port = addr.sin_port;
    hdr.op.new_interface.labels = htonl(hdr.op.new_interface.labels);
    int rc = send(bootstrap_sock, &hdr, sizeof(hdr), 0);
    if (rc != sizeof(hdr)) {
        throw -1;
    }
}

void CMMSocketImpl::send_local_listeners(int bootstrap_sock)
{
    size_t num_ifaces = htonl(local_ifaces.size());
    int rc = send(bootstrap_sock, &num_ifaces, sizeof(num_ifaces), 0);
    if (rc != sizeof(num_ifaces)) {
        throw -1;
    }

    for (ListenerAddrList::iterator it = local_ifaces.begin();
         it != local_ifaces.end(); it++) {
        send_local_listener(bootstrap_sock, it->addr);
    }
}


int
CMMSocketImpl::connection_bootstrap(const struct sockaddr *remote_addr, 
                                    socklen_t addrlen,
                                    int bootstrap_sock = -1)
{
    try {
        struct sockaddr_in bind_addr;
        memset(&bind_addr, 0, sizeof(bind_addr));
        bind_addr.sin_addr = INADDR_ANY;
        bind_addr.sin_port = 0;

        internal_listener_sock = socket(PF_INET, SOCK_STREAM, 0);
        if (internal_listener_sock < 0) {
            throw -1;
        }
        
        int rc = bind(internal_listener_sock, 
                      (struct sockaddr *)&bind_addr,
                      sizeof(bind_addr));
        if (rc < 0) {
            close(internal_listener_sock);
            throw rc;
        }
        rc = getsockname(internal_listener_sock, 
                         (struct sockaddr *)&bind_addr,
                         sizeof(bind_addr));
        if (rc < 0) {
            perror("getsockname");
            close(internal_listener_sock);
            throw rc;
        }
        internal_listen_port = bind_addr.sin_port;
        rc = listen(internal_listener_sock, 5);
        if (rc < 0) {
            close(internal_listener_sock);
            throw rc;
        }
        
        for (NetInterfaceList::iterator it = ifaces.begin();
             it != ifaces.end(); it++) {
            
            struct net_interface listener_addr;
            memset(&listener_addr, 0, sizeof(listener_addr));
            listener_addr.ip_addr = it->second.ip_addr;
            listener_addr.labels = it->labels;

            local_ifaces.push_back(listener_addr);
        }
        
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
                rc = connect(bootstrap_sock, remote_addr, addrlen);
                if (rc < 0) {
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
        close(internal_listener_sock);
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
        CMMSocketImplPtr tmp(new CMSocketImpl(family, type, protocol));
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

int
CMMSocketImpl::mc_close(mc_socket_t sock)
{
    CMMSockHash::accessor ac;
    if (cmm_sock_hash.find(ac, sock)) {
	cmm_sock_hash.erase(ac);
        /* the CMMSocket object gets destroyed by the shared_ptr. */
        /* moved the rest of the cleanup to the destructor */
	return 0;
    } else {
	fprintf(stderr, "Warning: cmm_close()ing a socket that's not "
		"in my hash\n");
	errno = EBADF;
	return -1;
    }
}


CMMSocketImpl::CMMSocketImpl(int family, int type, int protocol)
    : csocks(this)
{
    /* reserve a dummy OS file descriptor for this mc_socket. */
    sock = socket(family, type, protocol);
    if (sock < 0) {
	/* invalid params, or no more FDs/memory left. */
	throw sock; /* :-) */
    }

    /* so we can identify this FD as a mc_socket later */
    /* XXX: This won't work when we yank out Juggler. */
    set_socket_labels(sock, FAKE_SOCKET_MAGIC_LABELS);

    /* XXX: think about how to support things besides
     * (PF_INET, SOCK_STREAM, 0) e.g. PF_INET6? SOCK_DGRAM? 
     * (More for library robustness than interesting research.) 
     */
    sock_family = family;
    sock_type = type;
    sock_protocol = protocol;

    non_blocking=0;
}

CMMSocketImpl::~CMMSocketImpl()
{
    for (CSockList::iterator it = connected_csocks.begin();
	 it != connected_csocks.end(); it++) {
	CSocket *victim = *it;
	delete victim;
    }
    
    free(remote_addr);
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
	if (ac->second->remote_addr != NULL) {
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
    
    if (non_blocking) {
        /* TODO: fixup with new connection approaches */
        return non_blocking_connect(initial_labels);
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

	    CMMSocketImplPtr sk = ac->second;
	    assert(sk);
	    if (sk->get_real_fds(osfd_list) != 0) {
		/* XXX: what about the nonblocking case? */
		fprintf(stderr,
			"DBG: cmm_select on a disconnected socket\n");
		errno = EBADF;
		return -1;
	    }
	}
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
	    if (sk->get_real_fds(osfd_list) != 0) {
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

int CMMSocketImpl::block_until_available(u_long send_labels, u_long recv_labels,
                                         CSocket *& csock)
{
    /* TODO: implement */
    return CMM_FAILED;
}

int 
CMMSocketImpl::preapprove(u_long send_labels, u_long recv_labels,
			  resume_handler_t resume_handler, void *arg,
                          CSocket *& csock)
{
    int rc = 0;
 
    {   
	CMMSockHash::const_accessor ac;
	if (!cmm_sock_hash.find(ac, sock)) {
            assert(0);
	}
	
	if (!remote_addr) {
	    errno = ENOTCONN;
	    return CMM_FAILED;
	}
    }
    
    if (net_available(sock, send_labels, recv_labels)) {
	rc = prepare(send_labels, recv_labels, csock);
    } else {
	if (resume_handler) {
	    enqueue_handler(sock, send_labels, recv_labels, resume_handler, arg);
	    rc = CMM_DEFERRED;
	} else {
	    rc = block_until_available(send_labels, recv_labels, csock);
	}
    }
    
    return rc;
}

/* return the actual socket FD if we have a mapping; else assume 
 * that it is unmapped and thus it is already the actual FD. */
int 
CMMSocketImpl::get_osfd(u_long label) 
{
    CMMSockHash::const_accessor ac;
    if (!cmm_sock_hash.find(ac, sock)) {
        assert(0);
    }
    if (sock_color_hash.find(label) != sock_color_hash.end()) {
        CSocket *csock = sock_color_hash[label];
        assert(csock);
        return csock->osfd;
    } else {
        errno = EINVAL;
        return -1;
    }
}

ssize_t 
CMMSocketImpl::mc_send(const void *buf, size_t len, int flags,
		       u_long send_labels, u_long recv_labels,
		       resume_handler_t resume_handler, void *arg)
{
    int rc;

#ifdef IMPORT_RULES
    /** Rules Part 3: Update labels if a better interface is available**/
    /* TODO-IMPL: move/reference this code as necessary */
    labels = set_superior_label(sock, labels);	
#endif
    CSocket *csock = NULL;
    rc = preapprove(labels, resume_handler, arg, csock);
    if (rc < 0) {
        return rc;
    }
    assert(csock);
    return send(csock->osfd, buf, len, flags);
}

int 
CMMSocketImpl::mc_writev(const struct iovec *vec, int count,
			 u_long send_labels, u_long recv_labels,
                         resume_handler_t resume_handler, void *arg)
{
    int rc;
    
#ifdef IMPORT_RULES
    /** Rules Part 3: Update labels if a better interface is available**/
    /* TODO-IMPL: move/reference this code as necessary */
    labels = set_superior_label(sock, labels);	
#endif
    CSocket *csock = NULL;
    rc = preapprove(labels, resume_handler, arg, csock);
    if (rc < 0) {
	return rc;
    }
    
    assert(csock);
    return writev(csock->osfd, vec, count);
}

int
CMMSocketImpl::mc_shutdown(int how)
{
    int rc = 0;
    CMMSockHash::accessor ac;
    if (cmm_sock_hash.find(ac, sock)) {
	for (CSockList::iterator it = connected_csocks.begin();
	     it != connected_csocks.end(); it++) {
	    CSocket *csock = *it;
	    assert(csock);
	    assert(csock->osfd > 0);
            rc = shutdown(csock->osfd, how);
            if (rc < 0) {
                return rc;
            }
	}
    } else {
	errno = EBADF;
	rc = -1;
    }

    return rc;
}

void
CMMSocketImpl::interface_up(struct net_interface up_iface)
{
    ifaces.push_back(up_iface);

    for (CMMSockHash::iterator sk_iter = cmm_sock_hash.begin();
	 sk_iter != cmm_sock_hash.end(); sk_iter++) {
	CMMSockHash::const_accessor read_ac;
	if (!cmm_sock_hash.find(read_ac, sk_iter->first)) {
	    assert(0);
	}
	CMMSocketImplPtr sk = read_ac->second;
	assert(sk);
        read_ac.release();

	sk->setup(up_iface);
    }    
}

void
CMMSocketImpl::interface_down(struct net_interface down_iface)
{
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

	sk->teardown(down_iface);
    }
}

int
CMMSocketImpl::check_label(u_long send_labels, u_long recv_labels,
                           resume_handler_t fn, void *arg)
{
    int rc = -1;
    if (scout_net_available(sock, send_labels, recv_labels)) {
	rc = 0;
    } else {
	if (fn) {
	    enqueue_handler(sock, send_labels, recv_labels, fn, arg);
	    rc = CMM_DEFERRED;
	} else {
	    rc = CMM_FAILED;
	}
    }
    
    return rc;
}

void set_socket_labels(int osfd, u_long labels)
{
    int rc;
#if 1 /* debug */
    u_long old_labels = 0;
    socklen_t len = sizeof(old_labels);
    rc = getsockopt(osfd, SOL_SOCKET, SO_CONNMGR_LABELS, 
		    &old_labels, &len);
    if (rc < 0) {
	fprintf(stderr, "Warning: failed getting socket %d labels %lu\n",
		osfd, labels);
    } else {
      //fprintf(stderr, "old socket labels %lu ", old_labels);
    }
#endif
    //fprintf(stderr, "new socket labels %lu (socket %d)\n", labels, osfd);
    
    rc = setsockopt(osfd, SOL_SOCKET, SO_CONNMGR_LABELS,
                    &labels, sizeof(labels));
    if (rc < 0) {
	fprintf(stderr, "Warning: failed setting socket %d labels %lu\n",
		osfd, labels);
    }
}

/* MUST call with ac held */
CSocket *
CMMSocketImpl::get_readable_csock(CMMSockHash::const_accessor& ac)
{
    fd_set readfds;
    int maxosfd = -1;
    int rc;

    do {
        FD_ZERO(&readfds);
        FD_SET(sock, &readfds);
        mcSocketOsfdPairList dummy;
        rc = make_real_fd_set(sock + 1, &readfds, dummy, &maxosfd);
        if (rc < 0) {
            /* XXX: maybe instead call prepare() and then proceed */
            errno = ENOTCONN;
            return NULL;
        }
        
        struct timeval timeout, *ptimeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 0;
        ptimeout = (non_blocking ? &timeout : NULL);
        
        ac.release();
        unblock_select_signals();
        rc = select(maxosfd + 1, &readfds, NULL, NULL, ptimeout);
        block_select_signals();
        if (!cmm_sock_hash.find(ac, sock)) {
            assert(0);
        }
    } while (rc < 0 && errno == EINTR);
    
    if (rc < 0) {
        /* errno set by select */
        return NULL;
    } else if (rc == 0) {
        errno = EWOULDBLOCK;
        return NULL;
    }

    for (CSockSet::iterator it = connected_csocks.begin();
         it != connected_csocks.end(); it++) {
        CSocket *csock = *it;
        assert(csock);

        /* XXX: this returns the first of potentially many ready sockets.
         * This may be a starvation problem; may want to somehow
         * do this round-robin. */
        if (FD_ISSET(csock->osfd, &readfds)) {
            return csock;
        }
    }
    
    /* This is unreachable because the osfds in connected_csocks
     * are the only ones set in readfds, so if rc > 0, then one 
     * of them must have been set. */
    assert(0);
}

int 
CMMSocketImpl::mc_listen(int listener_sock, int backlog)
{
    int rc = listen(listener_sock, backlog);
    if (rc < 0) {
        return rc;
    }
    ListenerSockSet::const_accessor ac;
    (void)cmm_listeners.insert(ac, listener_sock);
}

mc_socket_t 
CMMSocketImpl::mc_accept(int listener_sock, 
                         struct sockaddr *addr, socklen_t *addrlen,
                         u_long *remote_labels)
{
    ListenerSockSet::const_accessor ac;
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
    CMMSocketImplPtr sk_impl(dynamic_cast<CMMSocketImplPtr>(sk));
    assert(get_pointer(sk_impl));
    int rc = sk_impl->connection_bootstrap(addr, *addrlen, sock);
    close(sock);
        
    if (rc < 0) {
        mc_close(mc_sock);
        return rc;
    }
    return mc_sock;
}

int 
CMMSocketImpl::mc_read(void *buf, size_t count)
{
    CMMSockHash::const_accessor ac;
    if (!cmm_sock_hash.find(ac, sock)) {
        // see CMMSocketPassThrough
        assert(0);

	//errno = EBADF;
	//return CMM_FAILED;
	//return read(sock, buf,count);
    }

    int osfd = -1;
    CSocket *csock = get_readable_csock(ac);
    if (!csock) {
	//errno = ENOTCONN;
	return -1;
    }
    osfd = csock->osfd;
    ac.release();
    return read(osfd, buf, count);
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

    if (connected_csocks.empty()) {
        struct sockopt &opt = sockopts[level][name];
        if (opt.optval) {
            *optlen = opt.optlen;
            memcpy(optval, opt.optval, opt.optlen);
            return 0;
        } else {
            /* last resort; we haven't set this opt on this socket before,
             * so just return the default for the dummy socket */
            return getsockopt(sock, level, optname, optval, optlen);
        }
    }

    /* socket options are set on all sockets, so just pick one */
    CSocket *csock = *(connected_csocks.begin());
    assert(csock);
    return getsockopt(csock->osfd, level, optname, optval, optlen);
}

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

    for (CSockList::iterator it = connected_csocks.begin(); 
         it != connected_csocks.end(); it++) {
	CSocket *csock = *it;
	assert(csock);
	assert(csock->osfd != -1);

        if(optname == O_NONBLOCK) {
            int flags;
            flags = fcntl(csock->osfd, F_GETFL, 0);
            flags |= O_NONBLOCK;
            (void)fcntl(csock->osfd, F_SETFL, flags);
            non_blocking = 1;
        } else {
            rc = setsockopt(csock->osfd, level, optname, optval, optlen);
        }
	
        if (rc < 0) {
            return rc;
        }
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
CMMSocketImpl::reset()
{
    CMMSockHash::accessor ac;
    if (!cmm_sock_hash.find(ac, sock)) {
        // see CMMSocketPassThrough
        assert(0);
    }

    if (non_blocking) {
        fprintf(stderr, 
                "WARNING: cmm_reset not implemented for "
                "non-blocking sockets!!!\n");
        return CMM_FAILED;
    }
    
    for (CSockSet::iterator it = connected_csocks.begin();
         it != connected_csocks.end(); it++) {
        CSocket *csock = *it;
        assert(csock);
        
        shutdown(csock->osfd, SHUT_RDWR);
        close(csock->osfd);
        csock->cur_label = 0;
        delete csock;
    }

    connected_csocks.clear();
    
    return 0;
}

int
CMMSocketImpl::non_blocking_connect(u_long initial_labels)
{
    CSocket *csock = NULL;
    if (initial_labels) {
        csock = sock_color_hash[initial_labels];
    } 
    assert(csock);     //Make sure programmers use existing labels
    csock->connected=1;  /* XXX: this needs a comment explaining
                          * what's going on. It looks like connect
                          * could fail and this flag would still be
                          * set to 1, but I'm assuming that this is
                          * okay because of non-blocking stuff. */
    csock->cur_label = initial_labels;
    connected_csocks.insert(csock);
    int rc = connect(csock->osfd, remote_addr, addrlen);
    return rc;
}

int 
CMMSocketImpl::get_real_fds(mcSocketOsfdPairList &osfd_list)
{
    if (connected_csocks.empty()) {
        return -1;
    }

    for (CSockSet::iterator it = connected_csocks.begin();
         it != connected_csocks.end(); it++) {
        CSocket *csock = *it;
        assert(csock && csock->connected);

        int osfd = csock->osfd;
        assert(osfd > 0);

        osfd_list.push_back(pair<mc_socket_t,int>(sock,osfd));
    }

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
    
    if (connected_csocks.empty()) {
        /* XXX: maybe instead call prepare() and then proceed */
        errno = ENOTCONN;
        return -1;
    } 

    CSocket *csock = *(connected_csocks.begin());
    assert (csock && csock->connected);

    return getpeername(csock->osfd, address, address_len);
}

int
CMMSocketImpl::prepare(u_long send_label, u_long recv_label)
{
    CMMSockHash::const_accessor read_ac;

    if (!cmm_sock_hash.find(read_ac, sock)) {
	assert(0);
    }

    CSocket *csock = NULL;
    int rc = csocks.new_csock_with_labels(send_label, recv_label, csock);
    return rc;
}

void
CMMSocketImpl::setup(struct net_interface iface)
{
    CMMSockHash::const_accessor read_ac;
    if (!cmm_sock_hash.find(read_ac, sock)) {
	assert(0);
    }
    assert(get_pointer(read_ac->second) == this);

    struct CMMSocketControlHdr hdr;
    hdr.type = htons(CMM_CONTROL_MSG_NEW_INTERFACE);
    hdr.op.new_interface_data.ip_addr = iface.ip_addr;
    hdr.op.new_interface_data.labels = iface.labels;
    
    send_control_message(hdr);
}

void
CMMSocketImpl::teardown(struct net_interface iface)
{
    CMMSockHash::const_accessor read_ac;
    if (!cmm_sock_hash.find(read_ac, sock)) {
	assert(0);
    }
    assert(get_pointer(read_ac->second) == this);
    
    vector<CSocket *> victims;
    for (CSockSet::iterator it = connected_csocks.begin();
         it != connected_csocks.end(); it++) {
        CSocket *csock = *it;
        assert(csock);
        if (csock->iface.ip_addr.s_addr == iface.ip_addr.s_addr) {
            victims.push_back(csock);
        }
    }

    read_ac.release();

    if (!victims.empty()) {
        CMMSockHash::accessor write_ac;
        if (!cmm_sock_hash.find(write_ac, sock)) {
            assert(0);
        }
        assert(this == get_pointer(write_ac->second));
        
        while (!victims.empty()) {
            CSocket *victim = victims.back();
            victims.pop_back();
            connected_csocks.erase(victim);
            delete victim; /* closes socket, cleans up */
        }
    }

    struct CMMSocketControlHdr hdr;
    hdr.type = htons(CMM_CONTROL_MSG_DOWN_INTERFACE);
    hdr.op.down_interface_data.ip_addr = iface.ip_addr;
    
    send_control_message(hdr);
}

int CMMSocketImpl::send_control_message(struct CMMSocketControlHdr hdr)
{
    CMMSockHash::const_accessor read_ac;
    if (!cmm_sock_hash.find(read_ac, sock)) {
	assert(0);
    }
    assert(get_pointer(read_ac->second) == this);

    /* make sure there's at least one connected csocket */
    /* new_csock_with_labels searches, creating if no match exists */
    CSocket *csock = csocks.new_csock_with_labels(0, 0);
    if (!csock) {
        return -1;
    }
    assert(csock->osfd > 0);
    int osfd = csock->osfd;
    read_ac.release();

    if (send(osfd, &hdr, sizeof(hdr), 0) != sizeof(hdr)) {
        return -1;
    }
    return 0;
}

/* only called with read accessor held on this */
bool
CMMSocketImpl::net_available(u_long send_labels, u_long recv_labels)
{
    bool local_found = false;
    for (NetInterfaceList::const_iterator it = local_ifaces.begin();
         it != local_ifaces.end(); it++) {
        if (send_labels == 0 || it->labels & send_labels) {
            local_found = true;
            break;
        }
    }
    if (!local_found) {
        return false;
    }
    for (NetInterfaceList::const_iterator it = remote_ifaces.begin();
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
