#include <stdio.h>
#include <signal.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <assert.h>
#include <map>
#include <vector>
#include <memory>
#include <stropts.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <fcntl.h>
using std::vector; using std::pair;
using std::auto_ptr;

#include <connmgr_labels.h>

#include "libcmm.h"
#include "libcmm_ipc.h"

#define CMM_TIMING
#ifdef CMM_TIMING
#include "timeops.h"

#include "tbb/mutex.h"
static tbb::mutex timing_mutex;
#define TIMING_FILE "/tmp/cmm_timing.txt"
static FILE *timing_file;
static int num_switches;
static int num_switches_to_bg;
static int num_switches_to_fg;
static struct timeval total_switch_time;
static struct timeval total_switch_time_to_bg;
static struct timeval total_switch_time_to_fg;

static struct timeval total_time_in_connect;
static struct timeval total_time_in_up_cb;
#endif

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
    int connected;
    
    csocket(int family, int type, int protocol) {
	osfd = socket(family, type, protocol);
	if (osfd < 0) {
	    /* out of file descriptors or memory at this point */
	    throw osfd;
	}
	cur_label = 0;
	connected = 0;
    }

    ~csocket() {
	if (osfd > 0) {
	    /* if it's a real open socket */
	    shutdown(osfd, SHUT_RDWR);
	    close(osfd);
	}
    }
};

typedef std::map<u_long, struct csocket *> CSockHash;
typedef std::vector<struct csocket *> CSockList;

struct sockopt {
    void *optval;
    socklen_t optlen;

    sockopt() : optval(NULL), optlen(0) {}
};

/* < optname, (optval, optlen) > */
typedef std::map<int, struct sockopt> SockOptNames;

/* < level, < optname, (optval, optlen) > > */
typedef std::map<int, SockOptNames> SockOptHash;


#define IMPORT_RULES /* see notes below about label "precedence" rules */

struct cmm_sock {
    mc_socket_t sock;
    CSockHash sock_color_hash;
    CSockList csocks;

    /* XXX: The Right Way to do this is to subclass cmm_sock.
     * Note to self: do that ASAP and remove this, along with 
     * the associated asserts. */
    int serial; /* 1 if only one real socket can be connected at a time.
                 * 0 if many can be connected at a time. */
		int non_blocking; /* 1 if non blocking, 0 otherwise*/
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

#ifdef IMPORT_RULES
    int connecting; /* true iff the cmm_socket is currently in the process
		     * of calling any label_up callback.  Used to ensure
		     * we don't accidentally pick a "superior" label
		     * in this case. */
#endif

    cmm_sock(int family, int type, int protocol);
    ~cmm_sock() {
	for (CSockList::iterator it = csocks.begin();
	     it != csocks.end(); it++) {
	    struct csocket *victim = *it;
	    delete victim;
	}

	free(addr);
	shutdown(sock, SHUT_RDWR);
	close(sock);
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

cmm_sock::cmm_sock(int family, int type, int protocol) 
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

    /* TODO (eventually): remove this and implement the flag in cmm_connect. */
    /* XXX: but see above XXX about subclassing */
    serial = 1;
		non_blocking=0;
    active_csock = NULL;
}

typedef concurrent_hash_map<u_long, struct labeled_thunk_queue *,
			    MyHashCompare<u_long> > ThunkHash;
static ThunkHash thunk_hash;

#ifdef IMPORT_RULES
/**The following used for providing label rules**/
#define RULE_FILE "/tmp/cmm_rules_for_labels"
typedef concurrent_queue<u_long> RuleQueue;
struct RuleStruct{
	RuleQueue rule_queue;
};

typedef concurrent_hash_map<u_long, struct RuleStruct *, MyHashCompare<u_long> > RuleHash;
static RuleHash rule_hash;

typedef concurrent_hash_map<u_long, u_long, MyHashCompare<u_long> > SuperiorLookUp; 
static SuperiorLookUp sup_look_up;
#endif

static void net_status_change_handler(int sig);
static int prepare_socket(mc_socket_t sock, u_long label);

static void libcmm_init(void) __attribute__((constructor));
static void libcmm_init(void)
{
#ifdef IMPORT_RULES
    /** Rules Part1: pupulate the rule_hash from the options file**/
    printf("Parsing the label rules file...\n");
    FILE * fp;
    fp = fopen(RULE_FILE,"r");
    if (fp == NULL) {
        printf("Could not open the rules file. Proceeding with default. \n");
    } else{	
        char str_buf[250];
        const char delimiters[] = " :";
        
        while(fgets(str_buf, sizeof(str_buf), fp)) {
            char* token;
            token = strtok(str_buf, delimiters);
            u_long temp_label = (u_long)strtol(token, NULL, 0);	
            
            printf("Preferences for interface: %lu \n", temp_label);
            
            SuperiorLookUp::accessor lookup_ac;
            if (!sup_look_up.find(lookup_ac, temp_label)) {
                sup_look_up.insert(lookup_ac, temp_label);
            }
            lookup_ac->second = temp_label;
            
            RuleHash::accessor hash_ac;
            if (!rule_hash.find(hash_ac, temp_label)) {
                rule_hash.insert(hash_ac, temp_label);	
                struct RuleStruct* new_rs = new struct RuleStruct;
                hash_ac->second = new_rs;
            }
            
            while((token = strtok(NULL,delimiters)) != NULL) {
                temp_label = (u_long)strtol(token, NULL, 0);
                printf("Use interface: %lu", temp_label);
                hash_ac->second->rule_queue.push(temp_label);
            }
            printf("\n\n");
        }
        
        fclose(fp);
    }
#endif
    
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

#ifdef CMM_TIMING
    tbb::mutex::scoped_lock(timing_mutex);
    num_switches = num_switches_to_bg = num_switches_to_fg = 0;
    timerclear(&total_switch_time);
    timerclear(&total_switch_time_to_bg);
    timerclear(&total_switch_time_to_fg);
    timerclear(&total_time_in_connect);
    timerclear(&total_time_in_up_cb);
    struct timeval now;
    TIME(now);
    timing_file = fopen(TIMING_FILE, "a");
    if (timing_file) {
	fprintf(timing_file, "*** Started new run at %ld.%06ld, PID %d\n",
		now.tv_sec, now.tv_usec, getpid());
    }
#endif
}


static void libcmm_deinit(void) __attribute__((destructor));
static void libcmm_deinit(void)
{
#ifdef CMM_TIMING
    {
	tbb::mutex::scoped_lock(timing_mutex);
	
	if (timing_file) {
	    struct timeval now;
	    TIME(now);
	    fprintf(timing_file, "*** Finished run at %ld.%06ld, PID %d;\n",
		    now.tv_sec, now.tv_usec, getpid());
	    fprintf(timing_file, 
		    "*** Total time spent switching labels: "
		    "%ld.%06ld seconds (%ld.%06ld bg->fg, %ld.%06ld fg->bg) in %d switches (%d bg->fg, %d fg->bg)\n",
		    total_switch_time.tv_sec, total_switch_time.tv_usec, 
		    total_switch_time_to_fg.tv_sec, total_switch_time_to_fg.tv_usec,
		    total_switch_time_to_bg.tv_sec, total_switch_time_to_bg.tv_usec,
		    num_switches, num_switches_to_fg, num_switches_to_bg);
	    fprintf(timing_file, 
		    "*** Total time spent in connect(): %ld.%06ld seconds\n",
		    total_time_in_connect.tv_sec, 
		    total_time_in_connect.tv_usec);
	    fprintf(timing_file, 
		    "*** Total time spent in up_cb: %ld.%06ld seconds\n",
		    total_time_in_up_cb.tv_sec, 
		    total_time_in_up_cb.tv_usec);
            fclose(timing_file);
	}
    }
#endif

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

    //fprintf(stderr, "Signalled by scout\n");
    
    /* bitmask of all available bit labels ORed together */
    u_long cur_labels = scout_receive_label_update();
    fprintf(stderr, "Got update message from scout, labels=%lu\n",
	    cur_labels);

    u_long new_up_labels;
    u_long new_down_labels;
    scout_labels_changed(&new_up_labels, &new_down_labels);
    fprintf(stderr, "New labels available: %lu\n", new_up_labels);
    fprintf(stderr, "Labels now unavailable: %lu\n", new_down_labels);

#ifdef IMPORT_RULES
    /** Rules Part 2: setup the sup_lookup map for quicker check of interface superiority**/
    for (RuleHash::iterator rule_iter = rule_hash.begin(); 
	 rule_iter != rule_hash.end(); rule_iter++){
	u_long head_label = rule_iter->first;
	
	SuperiorLookUp::accessor supper_ac;
	if (!sup_look_up.find(supper_ac, head_label))
	    continue;
	//return to original, new info comming from scout
	supper_ac->second = supper_ac->first;  
	
	struct RuleStruct* rule_struct  = rule_iter->second;
	for (RuleQueue::iterator q_iter = rule_struct->rule_queue.begin(); 
	     q_iter != rule_struct->rule_queue.end(); q_iter++) {
	    if(cur_labels & *q_iter){
		fprintf(stderr,"Label %lu will use interface with label %lu\n", 
			supper_ac->first, *q_iter);
		supper_ac->second = *q_iter;
		break;
	    }
	}
    }
#endif
    
    //fprintf(stderr, "Before:\n---\n");
    //print_thunks();

    /* put down the sockets connected on now-unavailable networks. */
    for (CMMSockHash::iterator sk_iter = cmm_sock_hash.begin();
	 sk_iter != cmm_sock_hash.end(); sk_iter++) {
	CMMSockHash::const_accessor read_ac;
	if (!cmm_sock_hash.find(read_ac, sk_iter->first)) {
	    assert(0);
	}
	struct cmm_sock *sk = read_ac->second;
	assert(sk);
	/* TODO-REPLACE: sk->teardown(new_down_labels); BEGIN */
	if (sk->serial) {
	    if (sk->active_csock &&
		sk->active_csock->cur_label & new_down_labels) {
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
		if (sk->active_csock->cur_label & new_down_labels) {
		    shutdown(sk->active_csock->osfd, SHUT_RDWR);
		    close(sk->active_csock->osfd);
		    sk->active_csock->osfd = socket(sk->sock_family, 
						    sk->sock_type,
						    sk->sock_protocol);
		    sk->active_csock->cur_label = 0;
		    sk->active_csock->connected = 0;
		    sk->active_csock = NULL;
		}
	    }
	} else {
	    assert(0); /* TODO: implement parallel mode */
	}
	/* TODO-REPLACE: sk->teardown(new_down_labels); END */
    }

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
		    th->fn(th->arg);
		    /* application was required to free() or save th->arg */
		}
		/* clean up finished/cancelled thunks */
		delete th;
	    }
	}
    }

    //fprintf(stderr, "After:\n---\n");
    //print_thunks();
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


    fprintf(stderr, "Registered thunk %p, arg %p on mc_sock %d label %lu.\n", 
	    fn, arg, sock, label);
    //print_thunks();
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

#ifdef IMPORT_RULES
/** checks to see if there is a preffered interface for this label **/
static u_long set_superior_label(mc_socket_t sock, u_long label)
{
    {
	CMMSockHash::const_accessor read_ac;
	if (cmm_sock_hash.find(read_ac, sock)) {
	    struct cmm_sock *sk = read_ac->second;
	    assert(sk);
	    if (sk->connecting) {
		/* ensure that label_up callbacks only use the real
		 * labels that they were invoked with */
		return label;
	    } else {
		/* ignore; will fail in sock_preapprove */
	    }
	}
    }

    SuperiorLookUp::const_accessor lookup_ac;
    if (!sup_look_up.find(lookup_ac, label)){
	//printf("in superior lookup returned: %lu\n",label );
	return label;
	
    }else{
	//printf("in superior lookup returned: %lu\n",lookup_ac->second );
	return lookup_ac->second;
    }
    
}
#endif
/*** CMM socket function wrappers ***/

ssize_t cmm_send(mc_socket_t sock, const void *buf, size_t len, int flags,
		 u_long labels, void (*resume_handler)(void*), void *arg)
{
    int rc;

#ifdef IMPORT_RULES
    /** Rules Part 3: Update labels if a better interface is available**/
    labels = set_superior_label(sock, labels);	
#endif
    rc = sock_preapprove(sock, labels, resume_handler, arg);
    if (rc < 0) {
        return rc;
    }
    printf("Sending with label %lu\n",labels);
    return send(get_osfd(sock, labels), buf, len, flags);
}

int cmm_writev(mc_socket_t sock, const struct iovec *vec, int count,
	       u_long labels, void (*resume_handler)(void*), void *arg)
{
    int rc;

#ifdef IMPORT_RULES
    /** Rules Part 3: Update labels if a better interface is available**/
    labels = set_superior_label(sock, labels);	
#endif
    rc = sock_preapprove(sock, labels, resume_handler, arg);
    if (rc < 0) {
	return rc;
    }
    
    return writev(get_osfd(sock, labels), vec, count);
}

/* assume the fds in mc_fds are mc_socket_t's.  
 * add the real osfds to os_fds, and
 * also put them in osfd_list, so we can iterate through them. 
 * maxosfd gets the largest osfd seen. */
static int make_real_fd_set(int nfds, fd_set *fds,
			    vector<pair<mc_socket_t,int> > &osfd_list, 
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
	    struct cmm_sock *sk = ac->second;
	    assert(sk);
            /* TODO-REPLACE: sk->make_real_fd_set */
	    if (sk->serial) {
		struct csocket *csock = sk->active_csock;
		if (csock) {
		    int osfd = csock->osfd;
		    if (osfd == -1) {
			errno = EBADF;
			return -1;
		    } else {
			osfd_list.push_back(pair<mc_socket_t,int>(s,osfd));
		    }
		} else {
                    /* XXX: what about the nonblocking case? */
		    fprintf(stderr,
			    "DBG: cmm_select on a disconnected socket\n");
		    errno = EBADF;
		    return -1;
		}
	    } else {
		assert(0); /* TODO: implement parallel mode */
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
static int make_mc_fd_set(fd_set *fds,
			  const vector<pair<mc_socket_t, int> >&osfd_list)
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

int cmm_select(mc_socket_t nfds, 
	       fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
	       struct timeval *timeout)
{
    int maxosfd = nfds;
    int rc;

    /* TODO-REPLACE: CMMSocket::mc_select */
    /* these lists will be populated with the mc_socket mappings
     * that were in the original fd_sets */
    vector<pair<mc_socket_t, int> > readosfd_list;
    vector<pair<mc_socket_t, int> > writeosfd_list;
    vector<pair<mc_socket_t, int> > exceptosfd_list;

    rc = 0;
    fd_set tmp_readfds, tmp_writefds, tmp_exceptfds;
    FD_ZERO(&tmp_readfds);
    FD_ZERO(&tmp_writefds);
    FD_ZERO(&tmp_exceptfds);

    if (readfds) {
	tmp_readfds = *readfds;
	rc += make_real_fd_set(nfds, &tmp_readfds, readosfd_list, &maxosfd);
    }
    if (writefds) {
	tmp_writefds = *writefds;
	rc += make_real_fd_set(nfds, &tmp_writefds, writeosfd_list, &maxosfd);
    }
    if (exceptfds) {
	tmp_exceptfds = *exceptfds;
	rc += make_real_fd_set(nfds, &tmp_exceptfds, exceptosfd_list, &maxosfd);
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
    rc -= make_mc_fd_set(&tmp_readfds, readosfd_list);
    rc -= make_mc_fd_set(&tmp_writefds, writeosfd_list);
    rc -= make_mc_fd_set(&tmp_exceptfds, exceptosfd_list);

    if (readfds)   { *readfds   = tmp_readfds;   }
    if (writefds)  { *writefds  = tmp_writefds;  }
    if (exceptfds) { *exceptfds = tmp_exceptfds; }

    return rc;
}

int cmm_poll(struct pollfd fds[], nfds_t nfds, int timeout)
{

	CMMSockHash::const_accessor ac;	
	for(nfds_t i=0; i<nfds; i++)
	{
		if(!cmm_sock_hash.find(ac, fds[i].fd))
			continue;												//this is a non mc_socket
		else {
			struct cmm_sock *sk = ac->second;
			assert(sk);
                        /* TODO-REPLACE: sk->poll */
			if (sk->serial) {
				struct csocket *csock = sk->active_csock;
				if (!csock || !csock->connected){
					errno = ENOTCONN;
					return -1;
				}
				fds[i].fd=csock->osfd;	
			}
			else
				assert(0);						//parallel not implemented yet
		}
	}

	ac.release();
	return poll(fds, nfds, timeout);
}

int cmm_getpeername(int socket, struct sockaddr *address, socklen_t *address_len)
{
    CMMSockHash::const_accessor ac;
    if (!cmm_sock_hash.find(ac, socket)) {
					return getpeername(socket, address, address_len);
    }
		    
		struct cmm_sock *sk = ac->second;
    assert(sk);
    /* TODO-REPLACE: sk->getpeername */
    if (sk->serial) {
				struct csocket *csock = sk->active_csock;
				if (!csock || !csock->connected) {
						errno = ENOTCONN;
						return -1;
				}
				return getpeername(csock->osfd,address, address_len);
    } 
		else {
				assert(0); //parallel mode not implemented
    }
	
}

int cmm_read(mc_socket_t sock, void *buf, size_t count)
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
    /* TODO-REPLACE: sk->read */
    if (sk->serial) {
	struct csocket *csock = sk->active_csock;
	if (!csock || !csock->connected) {
	    errno = ENOTCONN;
	    return -1;
	}
	osfd = csock->osfd;
	ac.release();
	return read(osfd, buf, count);
    } else {
	assert(0); /* TODO: implement parallel mode. (read is a bit tricky.) */
    }
}

int cmm_getsockopt(mc_socket_t sock, int level, int optname, 
		   void *optval, socklen_t *optlen)
{
    CMMSockHash::const_accessor ac;
    if (!cmm_sock_hash.find(ac, sock)) {
	return getsockopt(sock, level, optname, optval, optlen);
    }

		struct cmm_sock *sk = ac->second;
    assert(sk);
    /* TODO-REPLACE: sk->getsockopt */
    if (sk->serial) {
        struct csocket *csock = sk->active_csock;
        if (!csock || !csock->connected) {
            errno = ENOTCONN;
            return -1;
        }
        return getsockopt(csock->osfd, level, optname, optval, optlen);
    } 
    else {
        assert(0);   //Parallel mode not implemented
    }
}

int cmm_setsockopt(mc_socket_t sock, int level, int optname, 
		   const void *optval, socklen_t optlen)
{
		int rc = 0;
    CMMSockHash::accessor ac;
    if (!cmm_sock_hash.find(ac, sock)) {
        //I think this probably needs to be handled like read above.
	errno = EBADF;
	return CMM_FAILED; 
    }
    struct cmm_sock *sk = ac->second;
    assert(sk);

    for (CSockList::iterator it = sk->csocks.begin();
	 it != sk->csocks.end(); it++) {
	struct csocket *csock = *it;
	assert(csock);
	if (csock->osfd != -1) {
	    if(optname==O_NONBLOCK){
		
		int flags;
    		flags = fcntl(csock->osfd, F_GETFL, 0);
    		flags |= O_NONBLOCK;
    		(void)fcntl(csock->osfd, F_SETFL, flags);
		sk->non_blocking=1;
		
	    }
	    else
		rc = setsockopt(csock->osfd, level, optname, optval, optlen);
	    
	    if (rc < 0) {
		return rc;
	    }
	}
    }
    /* all succeeded */

    /* inserts if not present */
    struct sockopt &opt = sk->sockopts[level][optname];
    if (opt.optval) {
	free(opt.optval);
    }
    opt.optlen = optlen;
    opt.optval = malloc(optlen);
    assert(opt.optval);
    memcpy(opt.optval, optval, optlen);

    return 0;
}

/* these are all sockopts that have succeeded in the past. 
 * for now, let's assume they succeed again. 
 * this may be invalid; maybe some sockopts succeed on one interface
 * but fail on another?  not sure. XXX */
/* REQ: call with write lock on this cmm_sock */
static int set_all_sockopts(struct cmm_sock *sk, int osfd)
{
    assert(sk);

    if (osfd != -1) {
	for (SockOptHash::const_iterator i = sk->sockopts.begin();
	     i != sk->sockopts.end(); i++) {
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

/* make sure that sock is ready to send data with up_label. */
static int prepare_socket(mc_socket_t sock, u_long up_label)
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
	if (sk->serial && sk->active_csock) {
	    csock = sk->active_csock;
	    up_label = sk->active_csock->cur_label;
	} else {
	    /* no active csock, no label specified;
	     * just grab the first socket that exists and whose network
	     * is available */
	    assert(sk->serial); /* XXX: remove after subclassing for serial */
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
	
	if (sk->serial) {
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

		shutdown(sk->active_csock->osfd, SHUT_RDWR);
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
	} else {
	    assert(0); /* TODO: remove after implementing parallel mode */
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
		shutdown(csock->osfd, SHUT_RDWR);
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
	if (sk->serial) {
	    sk->active_csock = csock;
	} else {
	    assert(0); /* TODO: remove after implementing parallel mode */
	}
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
			
			shutdown(csock->osfd, SHUT_RDWR);
			close(csock->osfd);
			csock->osfd = socket(sk->sock_family, 
					     sk->sock_type,
					     sk->sock_protocol);
			csock->cur_label = 0;
			csock->connected = 0;
			if (sk->serial) {
			    sk->active_csock = NULL;
			} else {
			    assert(0); /* TODO: implement parallel mode */
			}
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

int cmm_connect(mc_socket_t sock, 
		const struct sockaddr *serv_addr, socklen_t addrlen, u_long labels,
		connection_event_cb_t label_down_cb,
		connection_event_cb_t label_up_cb,
		void *cb_arg)
{
    {    
	CMMSockHash::const_accessor ac;
	if (!cmm_sock_hash.find(ac, sock)) {
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
    if(sk->non_blocking==1) {
	struct csocket *csock = NULL;
	if (labels) {
	    csock = sk->sock_color_hash[labels];
	} 
	assert(csock);     //Make sure programmers use existing labels
	csock->connected=1;  /* XXX: this needs a comment explaining
			      * what's going on. It looks like connect
			      * could fail and this flag would still be
			      * set to 1, but I'm assuming that this is
			      * okay because of non-blocking stuff. */
	csock->cur_label = labels;
	sk->active_csock = csock;
	int rc = connect(csock->osfd, serv_addr, addrlen);
	return rc;
    }
    
    return 0;
}

mc_socket_t cmm_socket(int family, int type, int protocol)
{
    struct cmm_sock *new_sk = NULL;
    try {
	/* automatically clean up if cmm_sock() throws */
	auto_ptr<struct cmm_sock> ptr(new cmm_sock(family, type, protocol));
	
	new_sk = ptr.release();
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

int cmm_reset(mc_socket_t sock)
{
    CMMSockHash::accessor ac;
    if (cmm_sock_hash.find(ac, sock)) {
	struct cmm_sock *sk = ac->second;
	assert(sk);
	if (sk->non_blocking) {
	    fprintf(stderr, 
		    "WARNING: cmm_reset not implemented for "
		    "non-blocking sockets!!!\n");
	    return CMM_FAILED;
	}

        /* TODO-REPLACE: sk->reset */
	if (sk->serial) {
	    if (sk->active_csock) {
		struct csocket *csock = sk->active_csock;

		sk->active_csock = NULL;
		shutdown(csock->osfd, SHUT_RDWR);
		close(csock->osfd);
		csock->osfd = socket(sk->sock_family,
				     sk->sock_type,
				     sk->sock_protocol);
		csock->connected = 0;
		csock->cur_label = 0;
	    }
	} else {
	    assert(0); /* XXX: implement parallel mode. */
	}

	return 0;
    } else {
	/* no such mc_socket */
	return CMM_FAILED;
    }
}

int cmm_check_label(mc_socket_t sock, u_long label,
		    resume_handler_t fn, void *arg)
{
    return sock_preapprove(sock, label, fn, arg);
}

int cmm_shutdown(mc_socket_t sock, int how)
{
    int rc = 0;
    CMMSockHash::accessor ac;
    if (cmm_sock_hash.find(ac, sock)) {
	struct cmm_sock *sk = ac->second;
	assert(sk);
	for (CSockList::iterator it = sk->csocks.begin();
	     it != sk->csocks.end(); it++) {
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

	assert(sk);

	delete sk; /* moved the rest of the cleanup to the destructor */
	return 0;
    } else {
	fprintf(stderr, "Warning: cmm_close()ing a socket that's not "
		"in my hash\n");
	errno = EBADF;
	return -1;
    }
}

/* if deleter is non-NULL, it will be called on the handler's arg. */
int cmm_thunk_cancel(u_long label, 
		     void (*handler)(void*), void *arg,
		     void (*deleter)(void*))
{
    int thunks_cancelled = 0;
    ThunkHash::accessor hash_ac;
    if (!thunk_hash.find(hash_ac, label)) {
	return 0;
    }
    struct labeled_thunk_queue *tq = hash_ac->second;
    
    for (ThunkQueue::iterator it = tq->thunk_queue.begin();
	 it != tq->thunk_queue.end(); it++) {
	struct thunk *& victim = *it;
	if (victim->fn == handler && victim->arg == arg) {
	    if (deleter) {
		deleter(victim->arg);
	    }
	    victim->arg = NULL;
	    victim->fn = NULL;
	    victim->label = 0;
	    thunks_cancelled++;
	    /* this thunk will be cleaned up later */
	}
    }

    return thunks_cancelled;
}
