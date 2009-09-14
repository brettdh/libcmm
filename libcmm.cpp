#include <stdio.h>
#include <signal.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <map>
#include <vector>
#include <memory>
//#include <stropts.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <fcntl.h>
#include <fstream>
#include <string>
using std::ifstream; using std::string;
using std::vector; using std::pair;
using std::auto_ptr;

#include "libcmm.h"
#include "libcmm_ipc.h"
#include "pending_irob.h"
#include "debug.h"
#include "signals.h"

#include "cmm_timing.h"

#include "cmm_socket.h"
#include "thunks.h"
#include "cmm_thread.h"

#include "tbb/concurrent_hash_map.h"
#include "tbb/concurrent_queue.h"
#include "tbb/atomic.h"
using tbb::concurrent_hash_map;
using tbb::concurrent_queue;
using tbb::atomic;

#define CONFIG_FILE "/etc/cmm_config"

#ifdef IMPORT_RULES
/**The following used for providing label rules**/
#define RULE_FILE "/tmp/cmm_rules_for_labels"
typedef concurrent_queue<u_long> RuleQueue;
struct RuleStruct{
	RuleQueue rule_queue;
};

typedef concurrent_hash_map<u_long, struct RuleStruct *, IntegerHashCompare<u_long> > RuleHash;
static RuleHash rule_hash;

typedef concurrent_hash_map<u_long, u_long, IntegerHashCompare<u_long> > SuperiorLookUp; 
static SuperiorLookUp sup_look_up;
#endif

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
    set_debugging(false); // default: no dbgprintfs

    ifstream config_input(CONFIG_FILE);
    if (config_input) {
        string line;
        while (getline(config_input, line)) {
            size_t pos = line.find("debug");
            if (pos != string::npos) {
                set_debugging(true);
            }
        }
        config_input.close();
    }
    
    scout_ipc_init();
    signals_init();

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
//         printf("Exiting; %d PendingIROBs still exist\n",
//                PendingIROB::objs());

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

#if 0
    ThunkHash::iterator it;

    for (it = thunk_hash.begin(); it != thunk_hash.end(); it++) {
	struct labeled_thunk_queue *tq = it->second;
	delete tq;
    }
    thunk_hash.clear();
#endif

    scout_ipc_deinit();

    //CMMThread::join_all();
    dbgprintf("Main thread exiting.\n");
    pthread_exit(NULL);
}

/* Figure out how the network status changed and invoke all the 
 * queued handlers that can now be processed. */
void process_interface_update(struct net_interface iface, bool down)
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
    dbgprintf("Got update from scout: %s is %s\n",
	      inet_ntoa(iface.ip_addr), down?"down":"up");

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

    if (down) {
        /* put down the sockets connected on now-unavailable networks. */
        CMMSocket::interface_down(iface);
    } else {    
        /* fire thunks thunk'd on now-available network. */
        CMMSocket::interface_up(iface);
        fire_thunks();
    }

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
		 u_long send_labels, 
                 void (*resume_handler)(void*), void *arg)
{
    return CMMSocket::lookup(sock)->mc_send(buf, len, flags,
					    send_labels, 
                                            resume_handler, arg);
}

int cmm_writev(mc_socket_t sock, const struct iovec *vec, int count,
               u_long send_labels, 
               void (*resume_handler)(void*), void *arg)
{
    return CMMSocket::lookup(sock)->mc_writev(vec, count,
                                              send_labels, 
                                              resume_handler, arg);
}

int cmm_select(mc_socket_t nfds, 
	       fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
	       struct timeval *timeout)
{
    int rc = 0;
    do {
        rc = CMMSocket::mc_select(nfds, readfds, writefds, exceptfds, timeout);
        if (rc < 0 && errno == EINTR) {
            dbgprintf("Select interrupted by signal; retrying "
                    "(inside libcmm)\n");
        } else {
            dbgprintf("mc_select returned %d, errno=%d\n", rc, errno);
        }
    } while (rc < 0 && errno == EINTR);
    return rc;
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

int cmm_listen(int listener_sock, int backlog)
{
    return CMMSocket::mc_listen(listener_sock, backlog);
}

mc_socket_t cmm_accept(int listener_sock, 
                       struct sockaddr *addr, socklen_t *addrlen)
{
    return CMMSocket::mc_accept(listener_sock, addr, addrlen);
}

int cmm_read(mc_socket_t sock, void *buf, size_t count, u_long *recv_labels)
{
    return CMMSocket::lookup(sock)->mc_read(buf, count, recv_labels);
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
		const struct sockaddr *serv_addr, socklen_t addrlen)
{
    return CMMSocket::lookup(sock)->mc_connect(serv_addr, addrlen);
}

mc_socket_t cmm_socket(int family, int type, int protocol)
{
    return CMMSocket::create(family, type, protocol);
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
int cmm_thunk_cancel(mc_socket_t sock, u_long send_labels,
		     void (*handler)(void*), void *arg,
		     void (*deleter)(void*))
{
    return cancel_thunk(sock, send_labels, handler, arg, deleter);
}
