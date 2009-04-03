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

void
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


CMMSocketSerial::CMMSocketSerial()
{
    active_csock = NULL;
}

// XXX: may be decomposable into unique and common portions
// XXX: for serial/parallel mc_sockets
void
CMMSocketSerial::prepare(u_long label)
{
    CMMSockHash::const_accessor read_ac;
    CMMSockHash::accessor write_ac;

    if (!cmm_sock_hash.find(read_ac, sock)) {
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
	if (active_csock) {
	    csock = active_csock;
	    up_label = active_csock->cur_label;
	} else {
	    /* no active csock, no label specified;
	     * just grab the first socket that exists and whose network
	     * is available */
	    for (CSockHash::iterator iter = sock_color_hash.begin();
		 iter != sock_color_hash.end(); iter++) {
		u_long label = iter->first;
		struct csocket *candidate = iter->second;
		if (candidate && scout_net_available(label)) {
		    csock = candidate;
		    up_label = label;
		}
	    }
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
	assert(csock->cur_label == 0); /* only for multiplexing */
	
        //teardown();
        if (active_csock) {
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
            assert(write_ac->second == this);
            
            close(active_csock->osfd);
            active_csock->osfd = socket(sock_family, 
                                            sock_type,
                                            sock_protocol);
            active_csock->cur_label = 0;
            active_csock->connected = 0;
            active_csock = NULL;
            
        } else {
            read_ac.release();
            if (!cmm_sock_hash.find(write_ac, sock)) {
                assert(0);
            }
            assert(write_ac->second == this);
            assert(csock == sock_color_hash[up_label]);
        }
        // end teardown() code
	
	setAllSockopts(csock->osfd);
	
	/* connect new socket with current label */
	set_socket_labels(csock->osfd, up_label);
	fprintf(stderr, "About to connect socket, label=%lu\n", up_label);
        
        write_ac.release();
        if (!cmm_sock_hash.find(read_ac, sock)) {
            assert(0);
        }
        assert(read_ac->second == this);
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
        assert(write_ac->second == this);
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
#ifdef IMPORT_RULES
	connecting = 1;
#endif
	active_csock = csock;
	write_ac.release();

	if (label_up_cb) {
#ifdef CMM_TIMING
	    TIME(up_cb_start);
#endif
	    int rc = label_up_cb(sock, up_label, cb_arg);
#ifdef CMM_TIMING
	    TIME(up_cb_end);
#endif
#ifdef IMPORT_RULES
	    if (cmm_sock_hash.find(write_ac, sock)) {
		assert(write_ac->second == this);
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
			assert(write_ac->second == this);
			assert(csock == sock_color_hash[up_label]);
			
			close(csock->osfd);
			csock->osfd = socket(sock_family, 
					     sock_type,
					     sock_protocol);
			csock->cur_label = 0;
			csock->connected = 0;
			active_csock = NULL;
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
		
		fprintf(timing_file, "Switch %d at %ld.%06ld: %ld.%06ld; from %s to %s; ",
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

void CMMSocketSerial::teardown(u_long down_label)
{
    CMMSockHash::const_accessor read_ac;
    if (!cmm_sock_hash.find(read_ac, sock)) {
	assert(0);
    }
    assert(read_ac->second == this);
    
    if (sk->active_csock &&
	sk->active_csock->cur_label & down_label) {
	if (sk->label_down_cb) {
	    read_ac.release();
	    sk->label_down_cb(sk->sock, sk->active_csock->cur_label, 
			      sk->cb_arg);
	} else {
	    read_ac.release();
	}
	
	CMMSockHash::accessor write_ac;
	if (!cmm_sock_hash.find(write_ac, sk_iter->first)) {
	    assert(0);
	}
	assert(sk == write_ac->second);
        
	/* the down handler may have reconnected the socket,
	 * so make sure not to close it in that case */
	if (sk->active_csock->cur_label & down_label) {
	    close(sk->active_csock->osfd);
	    sk->active_csock->osfd = socket(sk->sock_family, 
					    sk->sock_type,
					    sk->sock_protocol);
	    sk->active_csock->cur_label = 0;
	    sk->active_csock->connected = 0;
	    sk->active_csock = NULL;
	}
    }
}


/* assume the fds in mc_fds are mc_socket_t's.  
 * add the real osfds to os_fds, and
 * also put them in osfd_list, so we can iterate through them. 
 * maxosfd gets the largest osfd seen. */
int 
CMMSocketSerial::makeRealFdSet(int nfds, fd_set *fds,
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

	    CMMSockPtr sk = ac->second;
	    assert(sk);
	    if (sk->getRealFds(osfd_list) != 0) {
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

int 
CMMSocketSerial::getRealFds(mcSocketOsfdPairList &osfd_list)
{
    if (active_csock) {
	int osfd = active_csock->osfd;
	if (osfd == -1) {
	    return -1;
	} else {
	    osfd_list.push_back(pair<mc_socket_t,int>(sock,osfd));
	    return 0;
	}
    } else {
	return -1;
    }
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

void
CMMSocketSerial::pollMapBack(struct pollfd *origfd, 
			     const struct pollfd *realfd)
{
    assert(origfd && realfd && origfd->fd == sock);

    /* XXX: is this assertion valid? */
    //assert(active_csock && active_csock->osfd == realfd->fd);

    /* no worries about duplicates here. whee! */
    origfd->revents = realfd->revents;
}

int 
CMMSocketSerial::mc_getpeername(int socket, struct sockaddr *address, 
				socklen_t *address_len)
{
    CMMSockHash::const_accessor ac;
    if (!cmm_sock_hash.find(ac, socket)) {
	/* pass-through for non-mc-sockets */
	return getpeername(socket, address, address_len);
    }
    
    struct cmm_sock *sk = ac->second;
    assert(sk);
    struct csocket *csock = sk->active_csock;
    if (!csock || !csock->connected) {
	errno = ENOTCONN;
	return -1;
    }
    return getpeername(csock->osfd,address, address_len);
}

int 
CMMSocketSerial::mc_read(mc_socket_t sock, void *buf, size_t count)
{
    CMMSockHash::const_accessor ac;
    if (!cmm_sock_hash.find(ac, sock)) {
	//errno = EBADF;
	//return CMM_FAILED;
	return read(sock, buf,count);
    }

    int osfd = -1;
    struct cmm_sock *sk = ac->second;
    assert(sk);
    struct csocket *csock = sk->active_csock;
    if (!csock || !csock->connected) {
	errno = ENOTCONN;
	return -1;
    }
    osfd = csock->osfd;
    ac.release();
    return read(osfd, buf, count);
}
