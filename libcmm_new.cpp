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

#include "cmm_timing.h"

#include "cmm_socket.h"
#include "thunks.h"

#include "tbb/concurrent_hash_map.h"
#include "tbb/concurrent_queue.h"
#include "tbb/atomic.h"
using tbb::concurrent_hash_map;
using tbb::concurrent_queue;
using tbb::atomic;

static struct sigaction old_action;
static struct sigaction ignore_action;
static struct sigaction net_status_change_action;

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

	}
    }
#endif

#if 0
    ThunkHash::iterator it;

    for (it = thunk_hash.begin(); it != thunk_hash.end(); it++) {
	struct labeled_thunk_queue *tq = it->second;
	delete tq;
    }
    thunk_hash.clear();
#endif
    
    scout_ipc_deinit();
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
    CMMSocket::put_label_down(new_down_labels);

    fire_thunks(cur_labels);

    //fprintf(stderr, "After:\n---\n");
    //print_thunks();
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
    return CMMSocket::lookup(sock)->mc_send(buf, len, flags,
					    labels, resume_handler, arg);
}

int cmm_writev(mc_socket_t sock, const struct iovec *vec, int count,
	       u_long labels, void (*resume_handler)(void*), void *arg)
{
    return CMMSocket::lookup(sock)->mc_writev(vec, count,
					      labels, resume_handler, arg);
}

int cmm_select(mc_socket_t nfds, 
	       fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
	       struct timeval *timeout)
{
    return CMMSocket::mc_select(nfds, readfds, writefds, exceptfds, timeout);
}

int cmm_poll(struct pollfd fds[], nfds_t nfds, int timeout)
{
    return CMMSocket::mc_poll(fds, nfds, timeout);
}

int cmm_getpeername(mc_socket_t sock, struct sockaddr *address, 
		    socklen_t *address_len)
{
    return CMMSocket::lookup(sock)->mc_getpeername(address, address_len);
}

int cmm_read(mc_socket_t sock, void *buf, size_t count)
{
    return CMMSocket::lookup(sock)->mc_read(buf, count);
}

int cmm_getsockopt(mc_socket_t sock, int level, int optname, 
		   void *optval, socklen_t *optlen)
{
    return CMMSocket::lookup(sock)->mc_getsockopt(level, optname, 
						  optval, optlen);
}

int cmm_setsockopt(mc_socket_t sock, int level, int optname, 
		   const void *optval, socklen_t optlen)
{
    return CMMSocket::lookup(sock)->mc_setsockopt(level, optname,
						  optval, optlen);
}

int cmm_connect(mc_socket_t sock, 
		const struct sockaddr *serv_addr, socklen_t addrlen, 
		u_long labels,
		connection_event_cb_t label_down_cb,
		connection_event_cb_t label_up_cb,
		void *cb_arg)
{
    return CMMSocket::lookup(sock)->mc_connect(serv_addr, addrlen, labels, 
					       label_down_cb, label_up_cb, 
					       cb_arg);
}

mc_socket_t cmm_socket(int family, int type, int protocol)
{
    return CMMSocket::create(family, type, protocol);
}

int cmm_reset(mc_socket_t sock)
{
    return CMMSocket::lookup(sock)->reset();
}

int cmm_check_label(mc_socket_t sock, u_long label,
		    resume_handler_t fn, void *arg)
{
    return CMMSocket::lookup(sock)->check_label(label, fn, arg);
}

int cmm_shutdown(mc_socket_t sock, int how)
{
    return CMMSocket::lookup(sock)->mc_shutdown(how);
}

int cmm_close(mc_socket_t sock)
{
    return CMMSocket::mc_close(sock);
}

/* if deleter is non-NULL, it will be called on the handler's arg. */
int cmm_thunk_cancel(u_long label, 
		     void (*handler)(void*), void *arg,
		     void (*deleter)(void*))
{
    return cancel_thunk(label, handler, arg, deleter);
}
