#include "cmm_socket.h"
#include "cmm_socket.private.h"
#include "libcmm.h"
#include "libcmm_ipc.h"
#include <connmgr_labels.h>
#include <sys/stat.h>

#include "thunks.h"

#include <map>
#include <vector>
#include <set>
using std::map; using std::vector;
using std::set;

CMMSockHash CMMSocketImpl::cmm_sock_hash;

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
CMMSocketImpl::create(int family, int type, int protocol)
{
    CMMSocketImplPtr new_sk;
    mc_socket_t new_sock = -1;
    try {
	/* automatically clean up if cmm_sock() throws */
	CMMSocketImplPtr tmp(new CMMSocketSerial(family, type, protocol));
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
	    CMMSocketImplPtr sk = ac->second;
	    assert(sk);
	    sk->poll_map_back(origfd, &realfds[i]);
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
    //fprintf(stderr, "new socket labels %lu\n", labels);

    rc = setsockopt(osfd, SOL_SOCKET, SO_CONNMGR_LABELS,
                    &labels, sizeof(labels));
    if (rc < 0) {
	fprintf(stderr, "Warning: failed setting socket %d labels %lu\n",
		osfd, labels);
    }
}
