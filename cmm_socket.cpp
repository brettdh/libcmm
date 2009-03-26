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

// XXX: in progress. 
void
CMMSocketSerial::prepare(u_long label)
{
    CMMSockHash::const_accessor read_ac;
    CMMSockHash::accessor write_ac;

    struct cmm_sock *sk = NULL;
    if (!cmm_sock_hash.find(read_ac, sock)) {
	assert(0); /* already checked in caller */
    }

    sk = read_ac->second;
    assert(sk);
    
    struct csocket *csock = NULL;
    if (up_label) {
	csock = sk->sock_color_hash[up_label];
	if (!csock) {
	    /* caller specified an invalid label */
	    errno = EINVAL;
	    return CMM_FAILED;
	}
    } else {
	if (sk->active_csock) {
	    csock = sk->active_csock;
	    up_label = sk->active_csock->cur_label;
	} else {
	    /* no active csock, no label specified;
	     * just grab the first socket that exists and whose network
	     * is available */
	    for (CSockHash::iterator iter = sk->sock_color_hash.begin();
		 iter != sk->sock_color_hash.end(); iter++) {
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
	
        if (sk->active_csock) {
            down_label = sk->active_csock->cur_label;
            read_ac.release();
            if (sk->label_down_cb) {
                /* XXX: check return value? */
                sk->label_down_cb(sock, sk->active_csock->cur_label,
                                  sk->cb_arg);
            }
            
            if (!cmm_sock_hash.find(write_ac, sock)) {
                assert(0);
            }
            assert(write_ac->second == sk);
            
            close(sk->active_csock->osfd);
            sk->active_csock->osfd = socket(sk->sock_family, 
                                            sk->sock_type,
                                            sk->sock_protocol);
            sk->active_csock->cur_label = 0;
            sk->active_csock->connected = 0;
            sk->active_csock = NULL;
            
        } else {
            read_ac.release();
            if (!cmm_sock_hash.find(write_ac, sock)) {
                assert(0);
            }
            assert(write_ac->second == sk);
            assert(csock == sk->sock_color_hash[up_label]);
        }
	
	set_all_sockopts(sk, csock->osfd);
	
	/* connect new socket with current label */
	set_socket_labels(csock->osfd, up_label);
	fprintf(stderr, "About to connect socket, label=%lu\n", up_label);
        
        write_ac.release();
        if (!cmm_sock_hash.find(read_ac, sock)) {
            assert(0);
        }
        assert(read_ac->second == sk);
        assert(csock == sk->sock_color_hash[up_label]);
        
#ifdef CMM_TIMING
	TIME(connect_start);
#endif
	int rc = connect(csock->osfd, sk->addr, sk->addrlen);
#ifdef CMM_TIMING
	TIME(connect_end);
#endif
        read_ac.release();
        if (!cmm_sock_hash.find(write_ac, sock)) {
          assert(0);
        }
        assert(write_ac->second == sk);
        assert(csock == sk->sock_color_hash[up_label]);
        
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
	sk->connecting = 1;
#endif
	sk->active_csock = csock;
	write_ac.release();

	if (sk->label_up_cb) {
#ifdef CMM_TIMING
	    TIME(up_cb_start);
#endif
	    int rc = sk->label_up_cb(sk->sock, up_label, sk->cb_arg);
#ifdef CMM_TIMING
	    TIME(up_cb_end);
#endif
#ifdef IMPORT_RULES
	    if (cmm_sock_hash.find(write_ac, sock)) {
		assert(write_ac->second == sk);
		sk->connecting = 0;
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
			assert(write_ac->second == sk);
			assert(csock == sk->sock_color_hash[up_label]);
			
			close(csock->osfd);
			csock->osfd = socket(sk->sock_family, 
					     sk->sock_type,
					     sk->sock_protocol);
			csock->cur_label = 0;
			csock->connected = 0;
			sk->active_csock = NULL;
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
    }
    
    return 0;
}
