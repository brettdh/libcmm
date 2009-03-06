#include <stdio.h>
#include <pthread.h>
#include <mqueue.h>
#include <signal.h>
#include <assert.h>
#include <math.h>

#include <queue>
using std::queue;

#include <sys/time.h>
#include <time.h>

#include <connmgr_labels.h>
#include "libcmm.h"
#include "libcmm_ipc.h"
#include "tbb/atomic.h"
#include "tbb/concurrent_hash_map.h"

#include "cdf_sampler.h"
#include <memory>
using std::auto_ptr;

using tbb::atomic;

struct subscriber_proc {
    pid_t pid;
    mqd_t mq_fd;
};

struct Hash {
    size_t hash(pid_t pid) const { return pid; }
    bool equal(pid_t p1, pid_t p2) const { return (p1==p2); }
};

typedef tbb::concurrent_hash_map<pid_t, subscriber_proc, 
				 Hash> SubscriberProcHash;
static SubscriberProcHash subscriber_procs;

static bool running;
static atomic<bool> iface_up;
static atomic<u_long> labels_available;

#define UP_LABELS (CONNMGR_LABEL_BACKGROUND|CONNMGR_LABEL_ONDEMAND)
#define DOWN_LABELS (CONNMGR_LABEL_ONDEMAND)

int notify_subscriber(pid_t pid, mqd_t mq_fd);

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
		rc = notify_subscriber(msg.data.pid, mq_fd);
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

int notify_subscriber(pid_t pid, mqd_t mq_fd)
{
    struct timespec timeout = {1,0};
    struct cmm_msg msg;
    msg.opcode = CMM_MSG_NET_STATUS_CHANGE;
    msg.data.labels = labels_available;
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

void notify_all_subscribers(void)
{
    int rc;
    queue<pid_t> failed_procs;
    
    for (SubscriberProcHash::iterator it = subscriber_procs.begin();
	 it != subscriber_procs.end(); it++) {
	rc = notify_subscriber(it->first, it->second.mq_fd);
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
    exit(-1);
}

void thread_sleep(double fseconds)
{
    double iseconds = 0.0;
    double fnseconds = modf(fseconds, &iseconds);
    struct timespec timeout, rem;
    timeout.tv_sec = (time_t)iseconds;
    timeout.tv_nsec = (long)(fnseconds*1000000000);
    rem.tv_sec = 0;
    rem.tv_nsec = 0;

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
    char *ifname = "background";

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
    iface_up = true;
    labels_available = UP_LABELS;

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
	iface_up = true;
	labels_available = UP_LABELS;
	fprintf(stderr, "%s is up for %lf seconds\n", ifname, up_time);
	notify_all_subscribers();
	//if (sleep(up_time) != 0) break;
	thread_sleep(up_time);
	if (!running) break;

	if (sampling) {
	    down_time = down_time_samples->sample();
	}
	iface_up = false;
	labels_available = DOWN_LABELS;
	fprintf(stderr, "%s is down for %lf seconds\n", ifname, down_time);
	notify_all_subscribers();
	//if (sleep(down_time) != 0) break;
	thread_sleep(down_time);
    }

    delete up_time_samples;
    delete down_time_samples;
    pthread_join(tid, NULL);
    fprintf(stderr, "Scout gracefully quit.\n");
    return 0;
}
