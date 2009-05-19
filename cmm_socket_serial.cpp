#include "cmm_socket.h"
#include "cmm_socket.private.h"
#include <connmgr_labels.h>
#include "libcmm.h"
#include "libcmm_ipc.h"
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>

#include <map>
using std::pair;

CMMSocketSerial::CMMSocketSerial(int family, int type, int flags)
    : CMMSocket(family, type, flags)
{
    active_csock = NULL;
}

int
CMMSocketSerial::non_blocking_connect(u_long initial_labels)
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
    active_csock = csock;
    int rc = connect(csock->osfd, addr, addrlen);
    return rc;
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

// XXX: may be decomposable into unique and common portions
// XXX: for serial/parallel mc_sockets
int
CMMSocketSerial::prepare(u_long up_label)
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
            assert(get_pointer(write_ac->second) == this);
            
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
            assert(get_pointer(write_ac->second) == this);
            assert(csock == sock_color_hash[up_label]);
        }
        // end teardown() code
	
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

void
CMMSocketSerial::teardown(u_long down_label)
{
    CMMSockHash::const_accessor read_ac;
    if (!cmm_sock_hash.find(read_ac, sock)) {
	assert(0);
    }
    assert(get_pointer(read_ac->second) == this);
    
    if (active_csock &&
	active_csock->cur_label & down_label) {
	if (label_down_cb) {
	    read_ac.release();
	    label_down_cb(sock, active_csock->cur_label, 
                          cb_arg);
	} else {
	    read_ac.release();
	}
	
	CMMSockHash::accessor write_ac;
	if (!cmm_sock_hash.find(write_ac, sock)) {
	    assert(0);
	}
	assert(this == get_pointer(write_ac->second));
        
	/* the down handler may have reconnected the socket,
	 * so make sure not to close it in that case */
	if (active_csock->cur_label & down_label) {
	    close(active_csock->osfd);
	    active_csock->osfd = socket(sock_family, 
                                        sock_type,
                                        sock_protocol);
	    active_csock->cur_label = 0;
	    active_csock->connected = 0;
	    active_csock = NULL;
	}
    }
}

int 
CMMSocketSerial::get_real_fds(mcSocketOsfdPairList &osfd_list)
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

void
CMMSocketSerial::poll_map_back(struct pollfd *origfd, 
			     const struct pollfd *realfd)
{
    assert(origfd && realfd && origfd->fd == sock);

    /* XXX: is this assertion valid? */
    //assert(active_csock && active_csock->osfd == realfd->fd);

    /* no worries about duplicates here. whee! */
    origfd->revents = realfd->revents;
}

int 
CMMSocketSerial::mc_getpeername(struct sockaddr *address, 
				socklen_t *address_len)
{
    CMMSockHash::const_accessor ac;
    if (!cmm_sock_hash.find(ac, sock)) {
        assert(0);

	/* pass-through for non-mc-sockets; now a layer above */
	// return getpeername(sock, address, address_len);
    }
    
    struct csocket *csock = active_csock;
    if (!csock || !csock->connected) {
	errno = ENOTCONN;
	return -1;
    }
    return getpeername(csock->osfd,address, address_len);
}

int 
CMMSocketSerial::mc_read(void *buf, size_t count)
{
    CMMSockHash::const_accessor ac;
    if (!cmm_sock_hash.find(ac, sock)) {
        assert(0);

	//errno = EBADF;
	//return CMM_FAILED;
	//return read(sock, buf,count);
    }

    int osfd = -1;
    struct csocket *csock = active_csock;
    if (!csock || !csock->connected) {
	errno = ENOTCONN;
	return -1;
    }
    osfd = csock->osfd;
    ac.release();
    return read(osfd, buf, count);
}

int 
CMMSocketSerial::mc_getsockopt(int level, int optname, 
                               void *optval, socklen_t *optlen)
{
    CMMSockHash::const_accessor ac;
    if (!cmm_sock_hash.find(ac, sock)) {
	//return getsockopt(sock, level, optname, optval, optlen);
        assert(0);
    }

    struct csocket *csock = active_csock;
    if (!csock || !csock->connected) {
        errno = ENOTCONN;
        return -1;
    }
    return getsockopt(csock->osfd, level, optname, optval, optlen);
}

int
CMMSocketSerial::mc_setsockopt(int level, int optname, 
                               const void *optval, socklen_t optlen)
{
    int rc = 0;
    CMMSockHash::accessor ac;
    if (!cmm_sock_hash.find(ac, sock)) {
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
CMMSocketSerial::reset()
{
    CMMSockHash::accessor ac;
    if (!cmm_sock_hash.find(ac, sock)) {
        assert(0);
    }

    if (non_blocking) {
        fprintf(stderr, 
                "WARNING: cmm_reset not implemented for "
                "non-blocking sockets!!!\n");
        return CMM_FAILED;
    }
    
    if (active_csock) {
        struct csocket *csock = active_csock;
        
        active_csock = NULL;
        shutdown(csock->osfd, SHUT_RDWR);
        close(csock->osfd);
        csock->osfd = socket(sock_family,
                             sock_type,
                             sock_protocol);
        csock->connected = 0;
        csock->cur_label = 0;
    }
    
    return 0;
}
