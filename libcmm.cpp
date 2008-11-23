#include <stdio.h>
#include <signal.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <assert.h>
#include <map>
#include <vector>

#include <connmgr_labels.h>

#include "libcmm.h"
#include "libcmm_ipc.h"

#include "tbb/concurrent_hash_map.h"
#include "tbb/concurrent_queue.h"
#include "tbb/atomic.h"
using tbb::concurrent_hash_map;
using tbb::concurrent_queue;
using tbb::atomic;

static struct sigaction old_action;
static struct sigaction ignore_action;
static struct sigaction net_status_change_action;

struct thunk {
    resume_handler_t fn;
    void *arg;
    u_long label; /* single label bit only; relax this in the future */
    mc_socket_t sock; /* the socket that this thunk was thunk'd on */

    thunk(void (*f)(void*), void *a, u_long lbl, mc_socket_t s) 
	: fn(f), arg(a), label(lbl), sock(s) {}
};

typedef concurrent_queue<struct thunk*> ThunkQueue;
struct labeled_thunk_queue {
    u_long label; /* single label bit only; relax this in the future */
    ThunkQueue thunk_queue;

    ~labeled_thunk_queue() {
	while (!thunk_queue.empty()) {
	    struct thunk *th = NULL;
	    thunk_queue.pop(th);
	    assert(th);
	    /* XXX: this leaks any outstanding thunk args.
	     * maybe we can assume that a thunk queue being destroyed means
	     * that the program is exiting. */
	    delete th;
	}
    }
};


struct csocket {
    int osfd;
    u_long cur_label;
    
    csocket() : osfd(-1), cur_label(0) {}
};

typedef std::map<u_long, struct csocket *> CSockHash;
typedef std::vector<struct csocket *> CSockList;

/* < optname, (optval, optlen) > */
typedef std::map<int, std::pair<void*,socklen_t> > SockOptNames;

/* < level, < optname, (optval, optlen) > > */
typedef std::map<int, SockOptNames> SockOptHash;

struct cmm_sock {
    mc_socket_t sock;
    CSockHash sock_color_hash;
    CSockList csocks;

    int serial; /* 1 if only one real socket can be connected at a time.
                 * 0 if many can be connected at a time. */
    struct csocket *active_csock; /* only non-NULL if this socket is serial. */

    int sock_family; /* these are used for re-creating the socket */
    int sock_type;
    int sock_protocol;
    SockOptHash sockopts;
    struct sockaddr *addr; /* these are used for reconnecting the socket */
    socklen_t addrlen;

    connection_event_cb_t label_down_cb;
    connection_event_cb_t label_up_cb;
    void *cb_arg;

    cmm_sock(int family, int type, int protocol);
    ~cmm_sock() {
	free(addr);
    }
};


template <typename T>
struct MyHashCompare {
    size_t hash(T sock) const { return (size_t)sock; }
    bool equal(T s1, T s2) const { return s1==s2; }
};

typedef concurrent_hash_map<mc_socket_t, 
			    struct cmm_sock*, 
			    MyHashCompare<mc_socket_t> > CMMSockHash;
static CMMSockHash cmm_sock_hash;
static atomic<u_long> next_mc_sock;

cmm_sock::cmm_sock(int family, int type, int protocol) 
{
    /* XXX: this could wrap around... eventually */
    sock = (mc_socket_t)next_mc_sock++; 

    sock_family = family;
    sock_type = type;
    sock_protocol = protocol;
    addr = NULL;
    addrlen = 0;

    label_down_cb = NULL;
    label_up_cb = NULL;
    cb_arg = NULL;
    
    /* TODO: read these from /proc instead of hard-coding them. */
    struct csocket *bg_sock = new struct csocket;
    struct csocket *ondemand_sock = new struct csocket;
    sock_color_hash[CONNMGR_LABEL_BACKGROUND] = bg_sock;
    sock_color_hash[CONNMGR_LABEL_ONDEMAND] = ondemand_sock;
    csocks.push_back(bg_sock);
    csocks.push_back(ondemand_sock);

    /* to illustrate how multiple labels can map to the same interface */
    sock_color_hash[CONNMGR_LABEL_RED] = bg_sock;
    sock_color_hash[CONNMGR_LABEL_BLUE] = ondemand_sock;

    /* TODO (eventually): remove this and implement the flag in cmm_connect. */
    serial = 1;
    active_csock = NULL;
}

typedef concurrent_hash_map<u_long, struct labeled_thunk_queue *,
			    MyHashCompare<u_long> > ThunkHash;
static ThunkHash thunk_hash;

static void net_status_change_handler(int sig);
static int prepare_socket(mc_socket_t sock, u_long label);

static void libcmm_init(void) __attribute__((constructor));
static void libcmm_init(void)
{
    memset(&ignore_action, 0, sizeof(ignore_action));
    memset(&net_status_change_action, 0, sizeof(net_status_change_action));
    memset(&old_action, 0, sizeof(old_action));

    ignore_action.sa_handler = SIG_IGN;
    net_status_change_action.sa_handler = net_status_change_handler;

    //sigaction(CMM_SIGNAL, &ignore_action, &old_action);
    sigaction(CMM_SIGNAL, &net_status_change_action, &old_action);
    if (old_action.sa_handler != SIG_DFL) {
	/* Unclear that this would ever happen, as this lib is probably
	 * loaded before the app registers a signal handler of its own.
	 * This places the burden on the app developer to avoid colliding
	 * with our signal of choice. */
	fprintf(stderr, 
		"WARNING: the application has changed the "
		"default handler for signal %d\n", CMM_SIGNAL);
    }

    scout_ipc_init(CMM_SIGNAL);
}


static void libcmm_deinit(void) __attribute__((destructor));
static void libcmm_deinit(void)
{
    ThunkHash::iterator it;

    for (it = thunk_hash.begin(); it != thunk_hash.end(); it++) {
	struct labeled_thunk_queue *tq = it->second;
	delete tq;
    }
    thunk_hash.clear();
    
    scout_ipc_deinit();
}

void print_thunks(void)
{
    for (ThunkHash::const_iterator tq_iter = thunk_hash.begin();
	 tq_iter != thunk_hash.end(); tq_iter++) {
	struct labeled_thunk_queue *tq = tq_iter->second;
	fprintf(stderr, "Label %lu, %d thunks\n",
		tq->label, tq->thunk_queue.size());
	for (ThunkQueue::const_iterator th_iter = tq->thunk_queue.begin();
	     th_iter != tq->thunk_queue.end(); th_iter++) {
	    struct thunk *th = *th_iter;
	    fprintf(stderr, "    Thunk %p, arg %p, label %lu\n",
		    th->fn, th->arg, th->label);
	}
    }
}

/* Figure out how the network status changed and invoke all the 
 * queued handlers that can now be processed. */
static void net_status_change_handler(int sig)
{
    /* 1) Read a message from the queue to determine what labels
     *    are available.
     * 2) For each available label, look through the queues for thunks
     *    with matching labels and execute the handlers, removing the thunks
     *    from the queues.  
     *    NOTE: we need to make sure this matching strategy
     *      is the same one employed by the kernel.  That's not really ideal.
     *    EDIT: well, sorta.  The kernel will eventually have to tell us
     *      what application-level labels an interface matches, even though
     *      that may change over time.
     * 3) Clean up.
     */

    fprintf(stderr, "Signalled by scout\n");
    
    /* bitmask of all available bit labels ORed together */
    u_long cur_labels = scout_receive_label_update();
    fprintf(stderr, "Got update message from scout, labels=%lu\n",
	    cur_labels);

    fprintf(stderr, "Before:\n---\n");
    print_thunks();

    ThunkQueue matches;
    
    /* Handlers are fired:
     *  -for the same label in the order they were enqueued, and
     *  -for different labels in arbitrary order. */
    for (ThunkHash::iterator tq_iter = thunk_hash.begin();
	 tq_iter != thunk_hash.end(); tq_iter++) {
	struct labeled_thunk_queue *tq = tq_iter->second;
	if (tq->label & cur_labels) {
	    while (!tq->thunk_queue.empty()) {
		struct thunk *th = NULL;
		tq->thunk_queue.pop(th);
		assert(th);
		if (th->fn) {
		    matches.push(th);
		} else {
		    /* clean up cancelled thunks */
		    delete th;
		}
	    }
	}
    }
    /* matches now contains all thunks that match the labels 
     * (including thunks on all sockets) */

    fprintf(stderr, "After:\n---\n");
    print_thunks();

    while (!matches.empty()) {
	struct thunk *th = NULL;
	matches.pop(th);
	assert(th);
	assert(th->fn);

	/* no need to do this anymore; this will get done as a side effect
	 * of any cmm_(stuff) the thunk does. */
	/* reconnect_socket(th->sock); */

	/* invoke application-level magic */
	th->fn(th->arg);
	/* application was required to free() or save th->arg */
	delete th;
    }
}

#ifndef SO_CONNMGR_LABELS
#define SO_CONNMGR_LABELS 39
#endif
static void set_socket_labels(int osfd, u_long labels)
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
	fprintf(stderr, "old socket labels %lu ", old_labels);
    }
#endif
    fprintf(stderr, "new socket labels %lu\n", labels);

    rc = setsockopt(osfd, SOL_SOCKET, SO_CONNMGR_LABELS,
			&labels, sizeof(labels));
    if (rc < 0) {
	fprintf(stderr, "Warning: failed setting socket %d labels %lu\n",
		osfd, labels);
    }
}

static void enqueue_handler(mc_socket_t sock, u_long label, 
			    void (*fn)(void*), void *arg)
{
    ThunkHash::accessor hash_ac;
    if (!thunk_hash.find(hash_ac, label)) {
	struct labeled_thunk_queue *new_tq = new struct labeled_thunk_queue;
	new_tq->label = label;
	thunk_hash.insert(hash_ac, label);
	hash_ac->second = new_tq;
    }

    struct thunk * new_thunk = new struct thunk(fn, arg, label, sock);

    hash_ac->second->thunk_queue.push(new_thunk);
}

static int sock_preapprove(mc_socket_t sock, u_long labels, 
			   void (*resume_handler)(void*), void *arg)
{
    int rc = 0;
 
    {   
	CMMSockHash::const_accessor ac;
	if (!cmm_sock_hash.find(ac, sock)) {
	    errno = EBADF;
	    return CMM_FAILED;
	    /* we should never get here, since cmm_socket MUST be called
	     * prior to any other cmm_function on a socket. EBADF makes
	     * sense in this situation, I think. */
	}
	
	if (!ac->second->addr) {
	    errno = ENOTCONN;
	    return CMM_FAILED;
	}
    }
    
    if (scout_net_available(labels)) {
	rc = prepare_socket(sock, labels);
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
static int get_osfd(mc_socket_t sock, u_long label) 
{
    CMMSockHash::const_accessor ac;
    if (!cmm_sock_hash.find(ac, sock)) {
	return -1;
    } else {
	if (ac->second->sock_color_hash.find(label) != 
	    ac->second->sock_color_hash.end()) {
	    struct csocket *csock = ac->second->sock_color_hash[label];
	    assert(csock);
	    return csock->osfd;
	} else {
	    errno = EINVAL;
	    return -1;
	}
    }
}

/*** CMM socket function wrappers ***/

ssize_t cmm_send(mc_socket_t sock, const void *buf, size_t len, int flags,
		 u_long labels, void (*resume_handler)(void*), void *arg)
{
    int rc;

    rc = sock_preapprove(sock, labels, resume_handler, arg);
    if (rc < 0) {
	return rc;
    }
    
    return send(get_osfd(sock, labels), buf, len, flags);
}

int cmm_writev(mc_socket_t sock, const struct iovec *vec, int count,
	       u_long labels, void (*resume_handler)(void*), void *arg)
{
    int rc;

    rc = sock_preapprove(sock, labels, resume_handler, arg);
    if (rc < 0) {
	return rc;
    }
    
    return writev(get_osfd(sock, labels), vec, count);
}

/* simple wrappers */
/* these just translate the socket to the underlying osfd and call the
 * original system call */

int cmm_getsockopt(mc_socket_t sock, int level, int optname, 
		   void *optval, socklen_t *optlen)
{
    return -1; /* TODO: implement for mc_sockets */
}

int cmm_setsockopt(mc_socket_t sock, int level, int optname, 
		   const void *optval, socklen_t optlen)
{
    return -1; /* TODO: implement for mc_sockets */
}

/* these are all sockopts that have succeeded in the past. 
 * for now, let's assume they succeed again. 
 * this may be invalid; maybe some sockopts succeed on one interface
 * but fail on another?  not sure. XXX */
static void set_all_sockopts(mc_socket_t sock, int osfd)
{
    /* TODO: implement */
}

/* make sure that sock is ready to send data with up_label. */
static int prepare_socket(mc_socket_t sock, u_long up_label)
{
    int newfd;

    CMMSockHash::accessor ac;
    struct cmm_sock *sk = NULL;
    if (cmm_sock_hash.find(ac, sock)) {
	sk = ac->second;
    } else {
	assert(0); /* already checked in caller */
    }
    
    struct csocket *csock = sk->sock_color_hash[up_label];
    assert(csock);
    if (csock->osfd == -1) {
	assert(csock->cur_label == 0); /* only for multiplexing */
	
	if (sk->serial) {
	    if (sk->active_csock) {
		/* XXX: check return value? */
		if (sk->label_down_cb) {
		    sk->label_down_cb(sock, sk->active_csock->cur_label,
				      sk->cb_arg);
		}
		close(sk->active_csock->osfd);
		sk->active_csock->osfd = -1;
		sk->active_csock->cur_label = 0;
	    }
	    
	    sk->active_csock = NULL;
	} else {
	    assert(0); /* TODO: remove after implementing parallel mode */
	}
	
	newfd = socket(sk->sock_family, sk->sock_type, sk->sock_protocol);
	if (newfd < 0) {
	    perror("socket");
	    fprintf(stderr, "libcmm: error creating new socket\n");
	    return newfd;
	}
	
	set_all_sockopts(sock, newfd);

	/* connect new socket with current label */
	set_socket_labels(newfd, up_label);
	if (connect(newfd, sk->addr, sk->addrlen) < 0) {
	    close(newfd);
	    fprintf(stderr, "libcmm: error connecting new socket\n");
	    /* we've previously checked, and the label should be
	     * available... so this failure is something else. */
	    /* XXX: maybe check scout_label_available(up_label) again? 
	     *      if it is not, return CMM_DEFERRED? */
	    /* XXX: this may be a race; i'm not sure. */
	    return -1;
	}
	/* XXX: check return value? */
	if (sk->label_up_cb) {
	    sk->label_up_cb(sk->sock, up_label, sk->cb_arg);
	}
	csock->osfd = newfd;
	csock->cur_label = up_label;
	if (sk->serial) {
	    sk->active_csock = csock;
	} else {
	    assert(0); /* TODO: remove after implementing parallel mode */
	}
    }
    
    return 0;
}

int cmm_connect(mc_socket_t sock, 
		const struct sockaddr *serv_addr, socklen_t addrlen,
		connection_event_cb_t label_down_cb,
		connection_event_cb_t label_up_cb,
		void *cb_arg)
{
    {    
	CMMSockHash::const_accessor ac;
	if (!cmm_sock_hash.find(ac, sock)) {
	    fprintf(stderr, 
		    "Error: tried to cmm_connect socket %p "
		    "not created by cmm_socket\n", sock);
	    errno = EBADF;
	    return CMM_FAILED; /* assert(0)? */
	}
	if (ac->second->addr != NULL) {
	    fprintf(stderr, 
		    "Warning: tried to cmm_connect an "
		    "already-connected socket %p\n", sock);
	    errno = EISCONN;
	    return CMM_FAILED;
	}

    }

    CMMSockHash::accessor ac;
    if (!cmm_sock_hash.find(ac, sock)) {
	/* already checked this above */
	assert(0);
    }
    
    struct cmm_sock *sk = ac->second;
    assert(sk);
    if (!sk->addr) {
	sk->addrlen = addrlen;
	sk->addr = (struct sockaddr *)malloc(addrlen);
	memcpy(sk->addr, serv_addr, addrlen);
	sk->label_down_cb = label_down_cb;
	sk->label_up_cb = label_up_cb;
	sk->cb_arg = cb_arg;
    } else {
	assert(0);
    }
    
    return 0;
}

int cmm_socket(mc_socket_t *sockp, int family, int type, int protocol)
{
    /* just for validating arguments */
    int s = socket(family, type, protocol);
    if (s < 0) {
	return s;
    }
    close(s);

    struct cmm_sock *new_sk = new cmm_sock(family, type, protocol);

    CMMSockHash::accessor ac;
    if (cmm_sock_hash.insert(ac, new_sk->sock)) {
	ac->second = new_sk;
    } else {
	fprintf(stderr, "Error: new socket %p is already in hash!  WTF?\n", 
		new_sk->sock);
	/* for now we will ignore the case in which someone creates 
	 * 2^32 (2^64) sockets. */
	assert(0);
    }

    *sockp = new_sk->sock;
    return 0;
}

int cmm_close(mc_socket_t sock)
{
    for (ThunkHash::iterator it = thunk_hash.begin();
	 it != thunk_hash.end(); it++) {
	struct labeled_thunk_queue *tq = it->second;
	for (ThunkQueue::iterator th_iter = tq->thunk_queue.begin();
	     th_iter != tq->thunk_queue.end(); th_iter++) {
	    struct thunk *th = *th_iter;
	    th->sock = (mc_socket_t)-1;
	    /* XXX: does it really make sense for thunks to exist in
	     * the absence of a mc_socket to execute them on? 
	     * Maybe we should just clear out the thunks here. 
	     * If we eventually move to just sending the data that 
	     * was waiting, maybe we will, since it's clear that 
	     * deferred sends that just result in the actual data
	     * being sent later should go away if the socket does. */
	}
    }

    CMMSockHash::accessor ac;
    if (cmm_sock_hash.find(ac, sock)) {
	struct cmm_sock *sk = ac->second;
	cmm_sock_hash.erase(ac);
	ac.release();

	sk->sock_color_hash.clear();
	for (CSockList::iterator it = sk->csocks.begin();
	     it != sk->csocks.end(); it++) {
	    struct csocket *victim = *it;
	    assert(victim);
	    if (victim->osfd > 0) {
		close(victim->osfd);
	    }
	    delete victim;
	}
	sk->csocks.clear();
	delete sk;
	return 0;
    } else {
	fprintf(stderr, "Warning: cmm_close()ing a socket that's not "
		"in my hash\n");
	errno = EBADF;
	return -1;
    }
}

/* if deleter is non-NULL, it will be called on the handler's arg. */
void cmm_thunk_cancel(u_long label, 
		      void (*handler)(void*), void (*deleter)(void*))
{
    ThunkHash::accessor hash_ac;
    if (!thunk_hash.find(hash_ac, label)) {
	return;
    }
    struct labeled_thunk_queue *tq = hash_ac->second;
    
    for (ThunkQueue::iterator it = tq->thunk_queue.begin();
	 it != tq->thunk_queue.end(); it++) {
	struct thunk *& victim = *it;
	if (victim->fn == handler) {
	    if (deleter) {
		deleter(victim->arg);
	    }
	    victim->arg = NULL;
	    victim->fn = NULL;
	    victim->label = 0;
	    /* this thunk will be cleaned up later */
	}
    }
}
