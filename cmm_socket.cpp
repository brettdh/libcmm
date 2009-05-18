#include "cmm_socket.h"

struct csocket {
    int osfd;
    u_long cur_label;
    int connected;
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
CMMSocket::create(int family, int type, int protocol)
{
    CMMSocketPtr new_sk;
    try {
	/* automatically clean up if cmm_sock() throws */
	new_sk = new CMMSocketSerial(family, type, protocol));
    } catch (int oserr) {
	return oserr;
    }

    CMMSockHash::accessor ac;
    if (cmm_sock_hash.insert(ac, new_sk->sock)) {
	ac->second = new_sk;
    } else {
	fprintf(stderr, "Error: new socket %d is already in hash!  WTF?\n", 
		new_sk->sock);
	assert(0);
    }

    return new_sk->sock;
}

CMMSocketPtr
CMMSocket::lookup(mc_socket_t sock)
{
    CMMSockHash::const_accessor read_ac;
    if (!cmm_sock_hash.find(read_ac, sock)) {
        return CMMSocketPtr(); /* works like NULL */
    } else {
        return read_ac->second;
    }
}

int
CMMSocket::close(mc_socket_t sock)
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

CMMSocket::CMMSocket(int family, int type, int protocol) 
{
    /* reserve a dummy OS file descriptor for this mc_socket. */
    sock = socket(family, type, protocol);
    if (sock < 0) {
	/* invalid params, or no more FDs/memory left. */
	throw sock; /* :-) */
    }

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

CMMSocket::~CMMSocket()
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
CMMSocket::mc_connect(mc_socket_t sock, 
		const struct sockaddr *serv_addr, socklen_t addrlen, 
		u_long initial_labels,
		connection_event_cb_t label_down_cb,
		connection_event_cb_t label_up_cb,
		void *cb_arg)
{
    /* TODO-COPY */
}

/* these are all sockopts that have succeeded in the past. 
 * for now, let's assume they succeed again. 
 * this may be invalid; maybe some sockopts succeed on one interface
 * but fail on another?  not sure. XXX */
/* REQ: call with write lock on this cmm_sock */
int 
CMMSocket::setAllSockopts(int osfd)
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

/* translate osfds back to mc_sockets.  Return the number of
 * duplicate mc_sockets. */
int 
CMMSocket::makeMcFdSet(fd_set *fds, const mcSocketOsfdPairList &osfd_list)
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
CMMSocket::mc_select(mc_socket_t nfds, 
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
	rc += makeRealFdSet(nfds, &tmp_readfds, readosfd_list, &maxosfd);
    }
    if (writefds) {
	tmp_writefds = *writefds;
	rc += makeRealFdSet(nfds, &tmp_writefds, writeosfd_list, &maxosfd);
    }
    if (exceptfds) {
	tmp_exceptfds = *exceptfds;
	rc += makeRealFdSet(nfds, &tmp_exceptfds, exceptosfd_list, &maxosfd);
    }

    if (rc < 0) {
	return -1;
    }

    rc = select(maxosfd + 1, &tmp_readfds, &tmp_writefds, &tmp_exceptfds, 
		timeout);
    if (rc < 0) {
	/* select does not modify the fd_sets if failure occurs */
	return rc;
    }

    /* map osfds back to mc_sockets, and correct for duplicates */
    rc -= makeMcFdSet(&tmp_readfds, readosfd_list);
    rc -= makeMcFdSet(&tmp_writefds, writeosfd_list);
    rc -= makeMcFdSet(&tmp_exceptfds, exceptosfd_list);

    if (readfds)   { *readfds   = tmp_readfds;   }
    if (writefds)  { *writefds  = tmp_writefds;  }
    if (exceptfds) { *exceptfds = tmp_exceptfds; }

    return rc;
}

int 
CMMSocket::mc_poll(struct pollfd fds[], nfds_t nfds, int timeout)
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
	    struct cmm_sock *sk = ac->second;
	    assert(sk);
	    if (sk->getRealFds(osfd_list) != 0) {
		errno = ENOTCONN;
		return -1;
	    }
	    for (int j = 0; j < osfd_list.size(); j++) {
		/* copy struct pollfd, overwrite fd */
		real_fds_list.push_back(fds[i]);
		real_fds_list.back().fd = osfd_list[i].second;
		osfds_to_pollfds[osfd_list[i].second] = &fds[i];
	    }
	}
    }

    nfds_t real_nfds = real_fds_list.size();
    struct pollfd *realfds = new struct pollfd[real_nfds];
    for (nfds_t i = 0; i < real_nfds; i++) {
	realfds[i] = real_fds_list[i];
    }

    int rc = poll(realfds, real_nfds, timeout);
    if (rc == 0) {
	return rc;
    }

    int lastfd = -1;
    for (nfds_t i = 0; i < real_nfds; i++) {
	struct pollfd *origfd = osfds_to_pollfds[realfds[i].fd];
	assert(origfd);
	CMMSockHash::const_accessor ac;	
	if(!cmm_sock_hash.find(ac, fds[i].fd)) {
	    origfd->revents = realfds[i].revents;
	} else {
	    CMMSocketPtr sk = ac->second;
	    assert(sk);
	    sk->pollMapBack(origfd, &realfds[i]);
	}
    }
    delete realfds;
}

int 
CMMSocket::preapprove(u_long labels, 
                      void (*resume_handler)(void*), void *arg)
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
CMMSocket::get_osfd(u_long label) 
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
CMMSocket::mc_send(const void *buf, size_t len, int flags,
                   u_long labels, void (*resume_handler)(void*), void *arg)
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
CMMSocket::mc_writev(const struct iovec *vec, int count,
                     u_long labels, void (*resume_handler)(void*), void *arg)
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
