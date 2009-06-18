#include <stdio.h>
#include <pthread.h>
#include <mqueue.h>
#include <signal.h>
#include <assert.h>
#include <math.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <queue>
using std::queue;
#include <vector>
#include <map>

#include <sys/time.h>
#include <time.h>

#include <connmgr_labels.h>
#include "libcmm.h"
#include "libcmm_ipc.h"
#include "tbb/atomic.h"
#include "tbb/concurrent_hash_map.h"
#include "common.h"

#include "cdf_sampler.h"
#include <memory>
using std::auto_ptr;

using tbb::atomic;

struct subscriber_proc {
    pid_t pid;
    mqd_t mq_fd;
};


typedef 
tbb::concurrent_hash_map<pid_t, subscriber_proc, 
                         IntegerHashCompare<pid_t> > SubscriberProcHash;
static SubscriberProcHash subscriber_procs;

static bool running;
static atomic<u_long> labels_available;

static pthread_mutex_t ifaces_lock = PTHREAD_MUTEX_INITIALIZER;

typedef std::vector<struct net_interface> IfaceList;
typedef std::map<in_addr_t, struct net_interface> NetInterfaceMap;
static NetInterfaceMap net_interfaces;

#define UP_LABELS (CONNMGR_LABEL_BACKGROUND|CONNMGR_LABEL_ONDEMAND)
#define DOWN_LABELS (CONNMGR_LABEL_ONDEMAND)

#define FG_IP_ADDRESS "10.0.0.42"
#define BG_IP_ADDRESS "10.0.0.2"


int notify_subscriber(pid_t pid, mqd_t mq_fd,
                      const IfaceList& changed_ifaces, 
                      const IfaceList& down_ifaces);

int init_subscriber(pid_t pid, mqd_t mq_fd)
{
    /* tell the subscriber about all the current interfaces. */
    IfaceList up_ifaces;
    pthread_mutex_lock(&ifaces_lock);
    for (NetInterfaceMap::const_iterator it = net_interfaces.begin();
         it != net_interfaces.end(); it++) {
        up_ifaces.push_back(it->second);
    }
    pthread_mutex_unlock(&ifaces_lock);

    return notify_subscriber(pid, mq_fd, up_ifaces, IfaceList());
}

/* REQ: ac must be bound to the desired victim by subscriber_procs.find() */
void remove_subscriber(SubscriberProcHash::accessor &ac)
{
    char proc_mq_name[MAX_PROC_MQ_NAMELEN];

    mq_close(ac->second.mq_fd);
    int len = snprintf(proc_mq_name, MAX_PROC_MQ_NAMELEN-1, 
		       SCOUT_PROC_MQ_NAME_FMT, ac->second.pid);
    assert(len>0);
    proc_mq_name[len] = '\0';
    mq_unlink(proc_mq_name);
    subscriber_procs.erase(ac);    
}

void * IPC_Listener(void *)
{
    int rc;
    char proc_mq_name[MAX_PROC_MQ_NAMELEN];

    struct mq_attr attr;
    attr.mq_flags = 0;
    attr.mq_maxmsg = 10;
    attr.mq_msgsize = sizeof(struct cmm_msg);
    mq_unlink(SCOUT_CONTROL_MQ_NAME);
    mqd_t scout_control_mq_fd = mq_open(SCOUT_CONTROL_MQ_NAME, O_CREAT|O_RDWR,
					0755, &attr);
    if (scout_control_mq_fd < 0) {
	perror("mq_open");
	fprintf(stderr, "Failed to open message queue\n");
	raise(SIGINT); /* easy way to bail out */
    }
    struct timespec timeout = {1, 0}; /* 1-second timeout */

    while (running) {
	/* listen for IPCs */
	struct cmm_msg msg;
	errno = 0;
	rc = mq_timedreceive(scout_control_mq_fd, (char*)&msg, sizeof(msg), 
			     NULL, &timeout);
	if (rc < 0) {
	    if (errno == EINTR || errno == ETIMEDOUT) {
		continue;
	    } else {
		perror("mq_receive");
		fprintf(stderr, "Failed to receive message on control queue\n");
		raise(SIGINT);
		break;
	    }
	}
	switch (msg.opcode) {
	case CMM_MSG_SUBSCRIBE:
	{
	    SubscriberProcHash::accessor ac;
	    if (subscriber_procs.find(ac, msg.data.pid)) {
		fprintf(stderr, 
			"Duplicate subscribe request received "
			"from process %d\n", msg.data.pid);
		fprintf(stderr, "Reopening message queue\n");
		mq_close(ac->second.mq_fd);
	    } else {
		subscriber_procs.insert(ac, msg.data.pid);
		ac->second.pid = msg.data.pid;
	    }
	    int len = snprintf(proc_mq_name, MAX_PROC_MQ_NAMELEN-1, 
			       SCOUT_PROC_MQ_NAME_FMT, msg.data.pid);
	    assert(len>0);
	    proc_mq_name[len] = '\0';
	    
	    mqd_t mq_fd = mq_open(proc_mq_name, O_WRONLY);
	    if (mq_fd < 0) {
		perror("mq_open");
		fprintf(stderr, "Failed opening message queue "
			"for process %d\n", msg.data.pid);
		subscriber_procs.erase(ac);
	    } else {
		rc = init_subscriber(msg.data.pid, mq_fd);
		if (rc < 0) {
		    mq_close(mq_fd);
		    subscriber_procs.erase(ac);
		} else {
		    ac->second.mq_fd = mq_fd;
		    fprintf(stderr, "Process %d subscribed\n", 
			    msg.data.pid);
		}
	    }
	    break;
	}
	case CMM_MSG_UNSUBSCRIBE:
	{
	    SubscriberProcHash::accessor ac;
	    if (subscriber_procs.find(ac, msg.data.pid)) {
		remove_subscriber(ac);
		fprintf(stderr, "Process %d unsubscribed\n", msg.data.pid);
	    } else {
		fprintf(stderr, 
			"Received request to unsubscribe "
			"unknown process %d, ignoring\n", msg.data.pid);
	    }
	    break;
	}
        /*
	case CMM_MSG_UPDATE_STATUS:
	{
	    SubscriberProcHash::accessor ac;
	    if (subscriber_procs.find(ac, msg.data.pid)) {
		int rc = notify_subscriber(msg.data.pid, ac->second.mq_fd);
		if (rc < 0) {
		    remove_subscriber(ac);
		}
	    } else {
		fprintf(stderr, "Received update request from "
			"unknown process %d, ignoring\n", msg.data.pid);
	    }
	    break;
	}
        */
	default:
	    fprintf(stderr, "Received unexpected message type %d, ignoring\n",
		    msg.opcode);
	    break;
	}
    }
    mq_close(scout_control_mq_fd);
    mq_unlink(SCOUT_CONTROL_MQ_NAME);
    return NULL;
}

int notify_subscriber_of_event(pid_t pid, mqd_t mq_fd, 
                               struct net_interface iface, MsgOpcode opcode)
{
    struct timespec timeout = {1,0};
    struct cmm_msg msg;
    msg.opcode = opcode;
    msg.data.iface = iface;
    int rc = mq_timedsend(mq_fd, (char*)&msg, sizeof(msg), 0, &timeout);
    if (rc < 0) {
	perror("mq_send");
	fprintf(stderr, "Failed to notify subscriber proc %d\n", pid);
    } else {
	fprintf(stderr, "Sent notification to process %d\n", pid);
	kill(pid, CMM_SIGNAL);
    }
    return rc;
}

int notify_subscriber(pid_t pid, mqd_t mq_fd,
                      const IfaceList& changed_ifaces, 
                      const IfaceList& down_ifaces)
{
    int rc;
    for (size_t i = 0; i < changed_ifaces.size(); i++) {
        const struct net_interface& iface = changed_ifaces[i];
        rc = notify_subscriber_of_event(pid, mq_fd, iface, 
                                        CMM_MSG_IFACE_LABELS);
        if (rc < 0) {
            return rc;
        }
    }
    for (size_t i = 0; i < down_ifaces.size(); i++) {
        const struct net_interface& iface = down_ifaces[i];
        rc = notify_subscriber_of_event(pid, mq_fd, iface, 
                                        CMM_MSG_IFACE_DOWN);
        if (rc < 0) {
            return rc;
        }
    }

    return 0;
}

void notify_all_subscribers(const IfaceList& changed_ifaces,
                            const IfaceList& down_ifaces)
{
    int rc;
    queue<pid_t> failed_procs;
    
    for (SubscriberProcHash::iterator it = subscriber_procs.begin();
	 it != subscriber_procs.end(); it++) {
	rc = notify_subscriber(it->first, it->second.mq_fd,
                               changed_ifaces, down_ifaces);
	if (rc < 0) {
	    failed_procs.push(it->first);
	}
    }
    SubscriberProcHash::accessor ac;
    while (!failed_procs.empty()) {
	pid_t victim = failed_procs.front();
	failed_procs.pop();
	if (subscriber_procs.find(ac, victim)) {
	    remove_subscriber(ac);
	}
	ac.release();
    }
}

void handle_term(int)
{
    fprintf(stderr, "Scout attempting to quit gracefully...\n");
    running = false;
}

void usage(char *argv[])
{
    fprintf(stderr, "Usage: %s <ap> [uptime downtime]\n", argv[0]);
    fprintf(stderr, "Usage:    uptime,downtime are in seconds.\n");
    fprintf(stderr, "\nUsage 2: %s <encounter duration cdf file> "
	    "\n                       <disconnect duration cdf file>\n", argv[0]);
    exit(-1);
}

void thread_sleep(double fseconds)
{
    double iseconds = -1.0;
    double fnseconds = modf(fseconds, &iseconds);
    struct timespec timeout, rem;
    timeout.tv_sec = (time_t)iseconds;
    timeout.tv_nsec = (long)(fnseconds*1000000000);
    rem.tv_sec = 0;
    rem.tv_nsec = 0;

    if (timeout.tv_sec < 0.0) {
	fprintf(stderr, "Error: fseconds negative (or just too big!)\n");
	raise(SIGINT);
    }

    while (nanosleep(&timeout, &rem) < 0) {
	if (!running) return;

	if (errno == EINTR) {
	    timeout.tv_sec = rem.tv_sec;
	    timeout.tv_nsec = rem.tv_nsec;
	} else {
	    perror("nanosleep");
	    return;
	}
    }

}

#define MIN_TIME 1

int main(int argc, char *argv[])
{
    if (argc != 3 && argc != 4) {
	usage(argv);
    }

    bool sampling = false;
    CDFSampler *up_time_samples = NULL;
    CDFSampler *down_time_samples = NULL;
    double presample_duration = 3600.0;
    const char *ifname = "background";

    double up_time = 30.0;
    double down_time = 5.0;
    if (argc == 4) {
	//ifname = argv[1];
	up_time = atof(argv[2]);
	down_time = atof(argv[3]);
	if (up_time < MIN_TIME || down_time < MIN_TIME) {
	    fprintf(stderr, 
		    "Error: uptime and downtime must be greater than "
		    "%u second.\n", MIN_TIME);
	    exit(-1);
	}
    } else if (argc == 3) {
        sampling = true;
	try {
	    auto_ptr<CDFSampler> up_ptr(new CDFSampler(argv[1], 
						       presample_duration));
	    auto_ptr<CDFSampler> down_ptr(new CDFSampler(argv[2],
							 presample_duration));
	    
	    up_time_samples = up_ptr.release();
	    down_time_samples = down_ptr.release();
	} catch (CDFErr &e) {
	    fprintf(stderr, "CDF Error: %s\n", e.str.c_str());
	    exit(1);
	}
    }
    
    signal(SIGINT, handle_term);

    running = true;
    labels_available = UP_LABELS;

    /* Add the interfaces on ruff, wizard-of-oz-style */
    const size_t num_ifs = 2;
    struct net_interface ifs[num_ifs] = {
        {{0}, CONNMGR_LABEL_ONDEMAND},
        {{0}, CONNMGR_LABEL_BACKGROUND}
    };
    const char *addrs[num_ifs] = {FG_IP_ADDRESS, BG_IP_ADDRESS};
    
    for (size_t i = 0; i < num_ifs; i++) {
        int rc = inet_aton(addrs[i], &ifs[i].ip_addr);
        assert(rc);
        net_interfaces[ifs[i].ip_addr.s_addr] = ifs[i];
    }
    
    struct net_interface bg_iface = ifs[1];
    net_interfaces.erase(bg_iface.ip_addr.s_addr); // will be added back first iteration
    IfaceList empty_list;
    IfaceList bg_iface_list;
    bg_iface_list.push_back(bg_iface);

    pthread_t tid;
    int rc = pthread_create(&tid, NULL, IPC_Listener, NULL);
    if (rc < 0) {
	perror("pthread_create");
	fprintf(stderr, "Couldn't create IPCListener thread, exiting\n");
	exit(-1);
    }

    while (running) {
	if (sampling) {
	    up_time = up_time_samples->sample();
	}
	labels_available = UP_LABELS;
        
	fprintf(stderr, "%s is up for %lf seconds\n", ifname, up_time);
        pthread_mutex_lock(&ifaces_lock);
        net_interfaces[bg_iface.ip_addr.s_addr] = bg_iface;
        pthread_mutex_unlock(&ifaces_lock);
	notify_all_subscribers(bg_iface_list, empty_list);

	thread_sleep(up_time);
	if (!running) break;

	if (sampling) {
	    down_time = down_time_samples->sample();
	}
	labels_available = DOWN_LABELS;
	fprintf(stderr, "%s is down for %lf seconds\n", ifname, down_time);

        pthread_mutex_lock(&ifaces_lock);
        net_interfaces.erase(bg_iface.ip_addr.s_addr);
        pthread_mutex_unlock(&ifaces_lock);

	notify_all_subscribers(empty_list, bg_iface_list);

	thread_sleep(down_time);
    }

    delete up_time_samples;
    delete down_time_samples;
    pthread_join(tid, NULL);
    fprintf(stderr, "Scout gracefully quit.\n");
    return 0;
}
