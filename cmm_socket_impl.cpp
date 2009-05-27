#include "cmm_socket.h"
#include "cmm_socket.private.h"
#include "libcmm.h"
#include "libcmm_ipc.h"
#include <connmgr_labels.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>

#include "thunks.h"
#include "cmm_timing.h"

#include <map>
#include <vector>
#include <set>
using std::map; using std::vector;
using std::set; using std::pair;

CMMSockHash CMMSocketImpl::cmm_sock_hash;

struct csocket {
    int osfd;
    u_long cur_label;
    int connected;

    csocket(int family, int type, int protocol);
    ~csocket();
};

csocket::csocket(int family, int type, int protocol) 
{
    osfd = socket(family, type, protocol);
    if (osfd < 0) {
	/* out of file descriptors or memory at this point */
	throw osfd;
    }
    cur_label = 0;
    connected = 0;
}

csocket::~csocket()
{
    if (osfd > 0) {
	/* if it's a real open socket */
	close(osfd);
    }
}

/* TODO: add parallel option */
mc_socket_t
CMMSocketImpl::create(int family, int type, int protocol, int cmm_flags)
{
    CMMSocketImplPtr new_sk;
    mc_socket_t new_sock = -1;
    try {
	/* automatically clean up if cmm_sock() throws */
        CMMSocketImplPtr tmp(new CMMSocketImpl(family, type, protocol, 
                                               cmm_flags));
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
 *   2) Must have our magic-number flag set.
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


CMMSocketImpl::CMMSocketImpl(int family, int type, int protocol,
                             int cmm_flags) 
{
    /* reserve a dummy OS file descriptor for this mc_socket. */
    sock = socket(family, type, protocol);
    if (sock < 0) {
	/* invalid params, or no more FDs/memory left. */
	throw sock; /* :-) */
    }

    if ((cmm_flags & CMM_FLAGS_SERIAL) &&
        (cmm_flags & CMM_FLAGS_PARALLEL)) {
        errno = EINVAL;
        throw sock;
    } else if (cmm_flags & CMM_FLAGS_SERIAL) {
        serial = true;
    } else if (cmm_flags & CMM_FLAGS_PARALLEL) {
        serial = false;
    } else {
        /* serial is the default */
        serial = true;
    }

    if (cmm_flags & CMM_FLAGS_APP_SETUP_ONLY_ONCE) {
        app_setup_only_once = true;
    } else {
        app_setup_only_once = false;
    }

    /* so we can identify this FD as a mc_socket later */
    set_socket_labels(sock, FAKE_SOCKET_MAGIC_LABELS);

    sock_family = family;
    sock_type = type;
    sock_protocol = protocol;
    addr = NULL;
    addrlen = 0;

    label_down_cb = NULL;
    label_up_cb = NULL;
    cb_arg = NULL;
    
    /* TODO: read these from /proc instead of hard-coding them. */
    struct csocket *bg_sock = new struct csocket(family, type, protocol);
    struct csocket *ondemand_sock = new struct csocket(family, type, protocol);

    sock_color_hash[CONNMGR_LABEL_BACKGROUND] = bg_sock;
    sock_color_hash[CONNMGR_LABEL_ONDEMAND] = ondemand_sock;
    csocks.push_back(bg_sock);
    csocks.push_back(ondemand_sock);

    /* to illustrate how multiple labels can map to the same interface */
    sock_color_hash[CONNMGR_LABEL_RED] = bg_sock;
    sock_color_hash[CONNMGR_LABEL_BLUE] = ondemand_sock;

    non_blocking=0;
}

CMMSocketImpl::~CMMSocketImpl()
{
    for (CSockList::iterator it = csocks.begin();
	 it != csocks.end(); it++) {
	struct csocket *victim = *it;
	delete victim;
    }
    
    free(addr);
    close(sock);
}

int 
CMMSocketImpl::mc_connect(const struct sockaddr *serv_addr, 
			  socklen_t addrlen_, 
			  u_long initial_labels,
			  connection_event_cb_t label_down_cb_,
			  connection_event_cb_t label_up_cb_,
			  void *cb_arg_)
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
	if (ac->second->addr != NULL) {
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
    
    if (!addr) {
	addrlen = addrlen_;
	addr = (struct sockaddr *)malloc(addrlen);
	memcpy(addr, serv_addr, addrlen_);
	label_down_cb = label_down_cb_;
	label_up_cb = label_up_cb_;
	cb_arg = cb_arg_;
    } else {
	assert(0);
    }

    if(non_blocking) {
        return non_blocking_connect(initial_labels);
    }
    
    return 0;
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


    rc = select(maxosfd + 1, &tmp_readfds, &tmp_writefds, &tmp_exceptfds, 
		timeout);
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
	
	if (!addr) {
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
		       u_long labels, 
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
			 u_long labels, resume_handler_t resume_handler, 
			 void *arg)
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
	for (CSockList::iterator it = csocks.begin();
	     it != csocks.end(); it++) {
	    struct csocket *csock = *it;
	    assert(csock);
	    if (csock->osfd > 0) {
		rc = shutdown(csock->osfd, how);
		if (rc < 0) {
		    return rc;
		}
	    }
	}
    } else {
	errno = EBADF;
	rc = -1;
    }

    return rc;
}

void
CMMSocketImpl::put_label_down(u_long down_label)
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

	sk->teardown(down_label);
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

    FD_ZERO(&readfds);
    FD_SET(sock, &readfds);
    mcSocketOsfdPairList dummy;
    int rc = make_real_fd_set(sock + 1, &readfds, dummy, &maxosfd);
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
    rc = select(maxosfd + 1, &readfds, NULL, NULL, ptimeout);
    if (!cmm_sock_hash.find(ac, sock)) {
        assert(0);
    }

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
        assert(csock && csock->connected);

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

    if (csocks.empty()) {
        assert(0);
    }

    /* socket options are set on all sockets, so just pick one */
    struct csocket *csock = *(csocks.begin());
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

    for (CSockList::iterator it = csocks.begin(); it != csocks.end(); it++) {
	struct csocket *csock = *it;
	assert(csock);
	if (csock->osfd != -1) {
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
        assert(csock && csock->connected);
        
        shutdown(csock->osfd, SHUT_RDWR);
        close(csock->osfd);
        csock->osfd = socket(sock_family,
                             sock_type,
                             sock_protocol);
        csock->connected = 0;
        csock->cur_label = 0;
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
    int rc = connect(csock->osfd, addr, addrlen);
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

#ifdef CMM_TIMING
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
                    if (candidate->connected ||
                        (!retry && scout_net_available(label))) {
                        csock = candidate;
                        up_label = label;
                    }
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
    if (!csock) {
	errno = ENOTCONN;
	return CMM_FAILED;
    }

    if (!csock->connected) {
#ifdef CMM_TIMING
	struct timeval switch_start;
	struct timeval switch_end;
	struct timeval connect_start;
	struct timeval connect_end;
	struct timeval up_cb_start;
	struct timeval up_cb_end;
	struct timeval diff;

	timerclear(&connect_start);
	timerclear(&connect_end);
	timerclear(&up_cb_start);
	timerclear(&up_cb_end);
	
	TIME(switch_start);
#endif
	u_long down_label = 0;

        /* only if serial: disconnect before connecting new socket */
        if (serial && !connected_csocks.empty()) {
            assert(connected_csocks.size() == 1);
            struct csocket *active_csock = *(connected_csocks.begin());

            down_label = active_csock->cur_label;
            read_ac.release();
            if (label_down_cb) {
                /* XXX: check return value? */
                label_down_cb(sock, active_csock->cur_label,
                                  cb_arg);
            }
            
            if (!cmm_sock_hash.find(write_ac, sock)) {
                assert(0);
            }
            assert(get_pointer(write_ac->second) == this);
            
            close(active_csock->osfd);
            active_csock->osfd = socket(sock_family, 
                                        sock_type,
                                        sock_protocol);
            active_csock->cur_label = 0;
            active_csock->connected = 0;
            connected_csocks.erase(active_csock);
        } else {
            read_ac.release();
            if (!cmm_sock_hash.find(write_ac, sock)) {
                assert(0);
            }
            assert(get_pointer(write_ac->second) == this);
            assert(csock == sock_color_hash[up_label]);
        }
	
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
        
#ifdef CMM_TIMING
	TIME(connect_start);
#endif
	int rc = connect(csock->osfd, addr, addrlen);
#ifdef CMM_TIMING
	TIME(connect_end);
#endif
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
#ifdef CMM_TIMING
		TIMEDIFF(connect_start, connect_end, diff);
		fprintf(timing_file, "connect() failed after %ld.%06ld seconds\n",
			diff.tv_sec, diff.tv_usec);
#endif
		return CMM_FAILED;
	    }
	}
	
	csock->cur_label = up_label;
	csock->connected = 1;

        connected_csocks.insert(csock);

	if (label_up_cb && 
            (!app_setup_only_once || connected_csocks.size() == 1)) {
#ifdef IMPORT_RULES
            connecting = 1;
#endif
            write_ac.release();
#ifdef CMM_TIMING
	    TIME(up_cb_start);
#endif
	    int rc = label_up_cb(sock, up_label, cb_arg);
#ifdef CMM_TIMING
	    TIME(up_cb_end);
#endif
#ifdef IMPORT_RULES
	    if (cmm_sock_hash.find(write_ac, sock)) {
		assert(get_pointer(write_ac->second) == this);
		connecting = 0;
		write_ac.release();
	    }
#endif

	    if (rc < 0) {
#ifdef CMM_TIMING
		TIMEDIFF(up_cb_start, up_cb_end, diff);
		fprintf(timing_file, 
			"error: application-level up_cb failed"
			"after %ld.%06ld\n",
			diff.tv_sec, diff.tv_usec);
#endif
		fprintf(stderr, "error: application-level up_cb failed\n");

		if (rc == CMM_DEFERRED) {
		    return rc;
		} else {
		    CMMSockHash::accessor write_ac;
		    if (cmm_sock_hash.find(write_ac, sock)) {
			assert(get_pointer(write_ac->second) == this);
			assert(csock == sock_color_hash[up_label]);
			
			close(csock->osfd);
			csock->osfd = socket(sock_family, 
					     sock_type,
					     sock_protocol);
			csock->cur_label = 0;
			csock->connected = 0;
                        connected_csocks.erase(csock);
		    } /* else: must have already been cmm_close()d */
		    
		    return CMM_FAILED;
		}
	    }
	}
#ifdef CMM_TIMING
	TIME(switch_end);
	{
	    tbb::mutex::scoped_lock(timing_mutex);
	    
	    if (timing_file) {
		TIMEDIFF(switch_start, switch_end, diff);
		struct timeval tmp = total_switch_time;
		timeradd(&tmp, &diff, &total_switch_time);
		if (down_label == CONNMGR_LABEL_BACKGROUND && 
		    up_label == CONNMGR_LABEL_ONDEMAND) {
		    num_switches_to_fg++;
		    tmp = total_switch_time_to_fg;
		    timeradd(&tmp, &diff, &total_switch_time_to_fg);
		} else if (down_label == CONNMGR_LABEL_ONDEMAND && 
			   up_label == CONNMGR_LABEL_BACKGROUND) {
		    num_switches_to_bg++;
		    tmp = total_switch_time_to_bg;
		    timeradd(&tmp, &diff, &total_switch_time_to_bg);
		}
		
		fprintf(timing_file, "Switch %d at %ld.%06ld: %ld.%06ld; "
                        "from %s to %s; ",
			++num_switches, 
			switch_start.tv_sec, switch_start.tv_usec,
			diff.tv_sec, diff.tv_usec,
			label_str(down_label), label_str(up_label));
		if (connect_start.tv_sec > 0) {
		    TIMEDIFF(connect_start, connect_end, diff);
		    fprintf(timing_file, "connect(): %ld.%06ld; ",
			    diff.tv_sec, diff.tv_usec);
		    struct timeval tmp = total_time_in_connect;
		    timeradd(&tmp, &diff, &total_time_in_connect);
		}
		if (up_cb_start.tv_sec > 0) {
		    TIMEDIFF(up_cb_start, up_cb_end, diff);
		    fprintf(timing_file, "up_cb(): %ld.%06ld",
			    diff.tv_sec, diff.tv_usec);
		    struct timeval tmp = total_time_in_up_cb;
		    timeradd(&tmp, &diff, &total_time_in_up_cb);
		}
		fprintf(timing_file, "\n");
	    }
	}
#endif
    } /* if (!csock->connected) */
    
    return 0;
}

void
CMMSocketImpl::teardown(u_long down_label)
{
    CMMSockHash::const_accessor read_ac;
    if (!cmm_sock_hash.find(read_ac, sock)) {
	assert(0);
    }
    assert(get_pointer(read_ac->second) == this);
    
    struct csocket *csock = sock_color_hash[down_label];
    if (csock && (csock->cur_label & down_label) && csock->connected) {
	if (label_down_cb) {
	    read_ac.release();
	    label_down_cb(sock, csock->cur_label, cb_arg);
	} else {
	    read_ac.release();
	}
	
	CMMSockHash::accessor write_ac;
	if (!cmm_sock_hash.find(write_ac, sock)) {
	    assert(0);
	}
	assert(this == get_pointer(write_ac->second));
        
        close(csock->osfd);
        csock->osfd = socket(sock_family, 
                                    sock_type,
                                    sock_protocol);
        csock->cur_label = 0;
        csock->connected = 0;
        connected_csocks.erase(csock);
    }
}
