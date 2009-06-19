#include "cmm_socket.h"
#include "cmm_socket.private.h"
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

struct csocket {
    int osfd;
    struct net_interface iface;
    u_long recv_label;
    CMMSocketImpl *msock;

    csocket(CMMSocketImpl *msock_, int accepted_sock);
    ~csocket();
};

csocket::csocket(CMMSocketImpl *msock_, struct net_interface iface_, 
                 u_long recv_label_, int accepted_sock = -1)
    : msock(msock_), iface(iface_), recv_label(recv_label_)
{
    assert(msock);
    if (accepted_sock == -1) {
        osfd = socket(msock->sock_family, msock->sock_type, msock->sock_protocol);
    } else {
        osfd = accepted_sock;
    }
    if (osfd < 0) {
	/* out of file descriptors or memory at this point */
	throw osfd;
    }
    cur_label = 0;
}

csocket::~csocket()
{
    if (osfd > 0) {
	/* if it's a real open socket */
	close(osfd);
    }
}

struct csocket *
CSockMapping::csock_with_send_label(u_long label)
{
    /* TODO */
}

struct csocket *
CSockMapping::csock_with_recv_label(u_long label)
{
    /* TODO */
}

struct csocket *
CSockMapping::csock_with_labels(u_long send_label, u_long recv_label)
{
    /* TODO */
}

struct csocket *
CSockMapping::new_csock_with_labels(u_long send_label, u_long recv_label)
{
    /* TODO */
}

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
    new_listener.listener_sock = -1; /* only valid for local listeners */
    remote_ifaces.push_back(new_listener);
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
        struct CMMSocketControlHdr hdr;
        hdr.type = htons(CMM_CONTROL_MSG_NEW_INTERFACE);
        hdr.op.new_interface.ip_addr = it->addr.sin_addr.s_addr;
        hdr.op.new_interface.port = it->addr.sin_port;
        hdr.op.new_interface.labels = htonl(hdr.op.new_interface.labels);
        int rc = send(bootstrap_sock, &hdr, sizeof(hdr), 0);
        if (rc != sizeof(hdr)) {
            throw -1;
        }
    }
}

int
CMMSocketImpl::connection_bootstrap(int bootstrap_sock = -1)
{
    /* TODO: start here (again).
     *  Rework this to have multiple listeners, local and remote.
     *  However, only one listener socket is needed, if I use
     *  INADDR_ANY.  It's like magic!
     *  Also, if sin_port = 0, it will be automatically assigned
     *  to an available port, which we can then get by getsockname.
     */
    try {
        for (ListenerAddrList::iterator it = ifaces.begin();
             it != ifaces.end(); it++) {
            
            ListenerAddr listener_addr;
            memset(&listener_addr, 0, sizeof(listener_addr));
            listener_addr.addr.sin_addr.s_addr = it->second.ip_addr;
            listener_addr.addr.sin_port = ;
            listener_addr.labels = it->labels;

            listener_addr.listener_sock = socket(PF_INET, SOCK_STREAM, 0);
            if (listener_addr.listener_sock < 0) {
                throw -1;
            }
    
            int rc = bind(listener_addr.listener_sock, 
                          (struct sockaddr *)&listener_addr.addr,
                          sizeof(listener_addr.addr));
            if (rc < 0) {
                close(listener_addr.listener_sock);
                throw rc;
            }
            rc = listen(listener_addr.listener_sock, 5);
            if (rc < 0) {
                close(listener_addr.listener_sock);
                throw rc;
            }
            local_ifaces.push_back(listener_addr);
        }
        
        if (bootstrap_sock != -1) {
            /* we are accepting a connection */
            send_local_listeners(bootstrap_sock);
        } else {
            /* we are connecting */
            assert(remote_addr);
            bootstrap_sock = socket(PF_INET, SOCK_STREAM, 0);
            if (bootstrap_sock < 0) {
                throw bootstrap_sock;
            }
            rc = connect(bootstrap_sock, remote_addr, addrlen);
            if (rc < 0) {
                close(bootstrap_sock);
                throw rc;
            }

            try {
                send_local_listener(bootstrap_sock);
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
{
    /* reserve a dummy OS file descriptor for this mc_socket. */
    sock = socket(family, type, protocol);
    if (sock < 0) {
	/* invalid params, or no more FDs/memory left. */
	throw sock; /* :-) */
    }

    /* so we can identify this FD as a mc_socket later */
    set_socket_labels(sock, FAKE_SOCKET_MAGIC_LABELS);

    sock_family = family;
    sock_type = type;
    sock_protocol = protocol;
    remote_addr = NULL;
    addrlen = 0;

    non_blocking=0;
}

CMMSocketImpl::~CMMSocketImpl()
{
    for (CSockList::iterator it = connected_csocks.begin();
	 it != connected_csocks.end(); it++) {
	struct csocket *victim = *it;
	delete victim;
    }
    
    free(remote_addr);
    close(sock);
}

int 
CMMSocketImpl::mc_connect(const struct sockaddr *serv_addr, 
                          socklen_t addrlen_, 
                          u_long initial_labels)
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
    
    if (!remote_addr) {
	addrlen = addrlen_;
	remote_addr = (struct sockaddr *)malloc(addrlen);
	memcpy(remote_addr, serv_addr, addrlen_);
    } else {
	assert(0);
    }

    if(non_blocking) {
        return non_blocking_connect(initial_labels);
    }

    int rc = connection_bootstrap();
    
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

int 
CMMSocketImpl::preapprove(u_long labels, 
			  resume_handler_t resume_handler, void *arg)
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
    
    if (scout_net_available(labels)) {
	rc = prepare(labels);
    } else {
	if (resume_handler) {
	    enqueue_handler(sock, labels, resume_handler, arg);
	    rc = CMM_DEFERRED;
	} else {
	    rc = CMM_FAILED;
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
        struct csocket *csock = sock_color_hash[label];
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
    rc = preapprove(labels, resume_handler, arg);
    if (rc < 0) {
        return rc;
    }
    printf("Sending with label %lu\n",labels);
    return send(get_osfd(labels), buf, len, flags);
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
    rc = preapprove(labels, resume_handler, arg);
    if (rc < 0) {
	return rc;
    }
    
    return writev(get_osfd(labels), vec, count);
}

int
CMMSocketImpl::mc_shutdown(int how)
{
    int rc = 0;
    CMMSockHash::accessor ac;
    if (cmm_sock_hash.find(ac, sock)) {
	for (CSockList::iterator it = connected_csocks.begin();
	     it != connected_csocks.end(); it++) {
	    struct csocket *csock = *it;
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
CMMSocketImpl::check_label(u_long labels, resume_handler_t fn, void *arg)
{
    int rc = -1;
    if (scout_net_available(labels)) {
	rc = 0;
    } else {
	if (fn) {
	    enqueue_handler(sock, labels, fn, arg);
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
struct csocket *
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
        struct csocket *csock = *it;
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
    int rc = sk_impl->connection_bootstrap(sock);
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
    struct csocket *csock = get_readable_csock(ac);
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
    struct csocket *csock = *(connected_csocks.begin());
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
	struct csocket *csock = *it;
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
        struct csocket *csock = *it;
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
    struct csocket *csock = NULL;
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
        struct csocket *csock = *it;
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

    struct csocket *csock = *(connected_csocks.begin());
    assert (csock && csock->connected);

    return getpeername(csock->osfd, address, address_len);
}

#if 0 /*def CMM_TIMING*/
static int floorLog2(unsigned int n) 
{
    int pos = 0;
    if (n >= 1<<16) { n >>= 16; pos += 16; }
    if (n >= 1<< 8) { n >>=  8; pos +=  8; }
    if (n >= 1<< 4) { n >>=  4; pos +=  4; }
    if (n >= 1<< 2) { n >>=  2; pos +=  2; }
    if (n >= 1<< 1) {           pos +=  1; }
    return ((n == 0) ? (-1) : pos);
}

static const char *label_strings[CONNMGR_LABEL_COUNT+1] = {"red", "blue", 
							   "ondemand", "background", 
							   "(invalid)"};

static const char *label_str(u_long label)
{
    int index = floorLog2(label);
    if (index < 0 || index >= CONNMGR_LABEL_COUNT) {
	index = CONNMGR_LABEL_COUNT; // "(invalid)" string
    }
    return label_strings[index];
}
#endif

int
CMMSocketImpl::prepare(u_long up_label)
{
    CMMSockHash::const_accessor read_ac;
    CMMSockHash::accessor write_ac;

    if (!cmm_sock_hash.find(read_ac, sock)) {
        // see CMMSocketPassThrough
	assert(0);
    }

    struct csocket *csock = NULL;
    /*TODO: should replace this entire if/else with this line:
      csock = csocks.lookup_by_send_label(up_label); */
    if (up_label) {
	csock = sock_color_hash[up_label];
	if (!csock) {
	    /* caller specified an invalid label */
	    errno = EINVAL;
	    return CMM_FAILED;
	}
    } else {
        bool retry = true;
        while (1) {
            /* just grab the first socket that exists and whose network
	     * is available, giving preference to a connected socket
             * if any are connected */
	    for (CSockHash::iterator iter = sock_color_hash.begin();
		 iter != sock_color_hash.end(); iter++) {
		u_long label = iter->first;
		struct csocket *candidate = iter->second;
		if (candidate) {
                    (!retry && scout_net_available(label))) {
                    csock = candidate;
                    up_label = label;
		}
	    }
            if (csock || !retry) {
                /* If found on the first try 
                 * or not found on the second, we're done
                 */
                break;
            }
            retry = false;
	}
    }
    if (!csock) {//TODO: reframe as addr=NULL
	errno = ENOTCONN;
	return CMM_FAILED;
    }

    if (!csock->connected) {//TODO: reframe under csock exists
        read_ac.release();
        if (!cmm_sock_hash.find(write_ac, sock)) {
            assert(0);
        }
        assert(get_pointer(write_ac->second) == this);
        assert(csock == sock_color_hash[up_label]);
	
	set_all_sockopts(csock->osfd);
	
	/* connect new socket with current label */
	set_socket_labels(csock->osfd, up_label);
	fprintf(stderr, "About to connect socket, label=%lu\n", up_label);
        
        write_ac.release();
        if (!cmm_sock_hash.find(read_ac, sock)) {
            assert(0);
        }
        assert(get_pointer(read_ac->second) == this);
        assert(csock == sock_color_hash[up_label]);
        
	int rc = connect(csock->osfd, remote_addr, addrlen);
        read_ac.release();
        if (!cmm_sock_hash.find(write_ac, sock)) {
            assert(0);
        }
        assert(get_pointer(write_ac->second) == this);
        assert(csock == sock_color_hash[up_label]);
        
	if (rc < 0) {
	    if(errno==EINPROGRESS || errno==EWOULDBLOCK)
		//is this what we want for the 'send', 
		//i.e wait until the sock is conn'ed.
		errno = EAGAIN;	 
	    else {
		perror("connect");
		close(csock->osfd);
		fprintf(stderr, "libcmm: error connecting new socket\n");
		/* we've previously checked, and the label should be
		 * available... so this failure is something else. */
		/* XXX: maybe check scout_label_available(up_label) again? 
		 *      if it is not, return CMM_DEFERRED? */
		/* XXX: this may be a race; i'm not sure. */

		return CMM_FAILED;
	    }
	}
	
	csock->cur_label = up_label;
	csock->connected = 1;

        connected_csocks.insert(csock);

        // to interrupt any select() in progress, adding the new osfd
        printf("Interrupting any selects() in progress to add osfd %d "
               "to multi-socket %d\n",
               csock->osfd, sock);
        signal_selecting_threads();
    } /* if (!csock->connected) */
    
    return 0;
}

void
CMMSocketImpl::setup(struct net_interface iface)
{
    CMMSockHash::const_accessor read_ac;
    if (!cmm_sock_hash.find(read_ac, sock)) {
	assert(0);
    }
    assert(get_pointer(read_ac->second) == this);

    /* TODO */
}

void
CMMSocketImpl::teardown(struct net_interface iface)
{
    CMMSockHash::const_accessor read_ac;
    if (!cmm_sock_hash.find(read_ac, sock)) {
	assert(0);
    }
    assert(get_pointer(read_ac->second) == this);
    
    vector<struct csocket *> victims;
    for (CSockSet::iterator it = connected_csocks.begin();
         it != connected_csocks.end(); it++) {
        struct csocket *csock = *it;
        assert(csock);
        if (csock->iface.ip_addr.s_addr == iface.ip_addr.s_addr) {
            victims.push_back(csock);
        }
    }

    if (victims.empty()) {
        return;
    }

    read_ac.release();
    CMMSockHash::accessor write_ac;
    if (!cmm_sock_hash.find(write_ac, sock)) {
        assert(0);
    }
    assert(this == get_pointer(write_ac->second));

    while (!victims.empty()) {
        struct csocket *victim = victims.back();
        victims.pop_back();
        connected_csocks.erase(victim);
        delete victim; /* closes socket, cleans up */
    }
}
