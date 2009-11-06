#include <stdio.h>
#include <pthread.h>
#include <mqueue.h>
#include <signal.h>
#include <assert.h>
#include <math.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <string.h>
#include <stdlib.h>

#include <queue>
#include <deque>
#include <vector>
#include <map>
using std::queue; using std::deque;

#include <sys/time.h>
#include <time.h>
#include "timeops.h"

#include "libcmm.h"
#include "libcmm_ipc.h"
#include "tbb/atomic.h"
#include "tbb/concurrent_hash_map.h"
#include "common.h"
#include "net_interface.h"

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
//static atomic<u_long> labels_available;

static pthread_mutex_t ifaces_lock = PTHREAD_MUTEX_INITIALIZER;

typedef std::vector<struct net_interface> IfaceList;
typedef std::map<in_addr_t, struct net_interface> NetInterfaceMap;
static NetInterfaceMap net_interfaces;

//#define UP_LABELS (CMM_LABEL_BACKGROUND|CMM_LABEL_ONDEMAND)
//#define DOWN_LABELS (CMM_LABEL_ONDEMAND)

// #define FG_IP_ADDRESS "10.0.0.42"
// #define BG_IP_ADDRESS "10.0.0.2"

#define EMULATION_BOX_IP   "10.0.0.12"
#define EMULATION_BOX_PORT 4422


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
    mode_t default_mode = umask(0);
    mqd_t scout_control_mq_fd = mq_open(SCOUT_CONTROL_MQ_NAME, O_CREAT|O_RDWR,
					SCOUT_PROC_MQ_MODE, &attr);
    (void)umask(default_mode);
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
	//kill(pid, CMM_SIGNAL);
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
    fprintf(stderr, 
            "Usage: conn_scout <FG iface> <bandwidth> <RTT>\n"
            "                  [<BG iface> <bandwidth> <RTT>\n"
            "                   [uptime downtime]]\n");
    fprintf(stderr, 
            "Usage:    uptime, downtime are in seconds;\n"
            "          bandwidth=bytes/sec, RTT=ms.\n");
    fprintf(stderr, 
            "Usage 2: conn_scout <FG iface> <bandwidth> <RTT>\n"
            "                    <BG iface> <bandwidth> <RTT>\n"
	    "                    cdf <encounter duration cdf file>\n"
	    "                        <disconnect duration cdf file>\n");
    fprintf(stderr, 
            "Usage 3: conn_scout replay <FG iface> <BG iface>\n"
            "   -Connects to the emulation box, which will transmit\n"
            "     the trace of network measurements.\n");
    exit(-1);
}

void thread_sleep(struct timeval tv);

void thread_sleep(double fseconds)
{
    double iseconds = -1.0;
    double fnseconds = modf(fseconds, &iseconds);
    struct timeval timeout;
    timeout.tv_sec = (time_t)iseconds;
    timeout.tv_usec = (long)(fnseconds*1000000);

    if (fseconds <= 0.0) {
	fprintf(stderr, "Error: fseconds <= 0.0 (or just too big!)\n");
	raise(SIGINT);
    }

    thread_sleep(timeout);
}

void thread_sleep(struct timeval tv)
{
    struct timespec timeout, rem;
    timeout.tv_sec = tv.tv_sec;
    timeout.tv_nsec = tv.tv_usec * 1000;
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

int get_ip_address(const char *ifname, struct in_addr *ip_addr)
{
    if (strlen(ifname) > IF_NAMESIZE) {
	fprintf(stderr, "Error: ifname too long (longer than %d)\n", IF_NAMESIZE);
	return -1;
    }

    struct ifreq ifr;
    strcpy(ifr.ifr_name, ifname);
    ifr.ifr_addr.sa_family = AF_INET;

    int sock = socket(PF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
	perror("socket");
	return sock;
    }
    int rc = ioctl(sock, SIOCGIFADDR, &ifr);
    if (rc < 0) {
	close(sock);
	return rc;
    }

    struct sockaddr_in *inaddr = (struct sockaddr_in*)&ifr.ifr_addr;
    memcpy(ip_addr, &inaddr->sin_addr, sizeof(ip_addr));

    close(sock);
    return rc;
}

struct trace_slice {
    struct timeval start;
    u_long wifi_bw_down;
    u_long wifi_bw_up;
    u_long wifi_RTT;
    u_long cellular_bw_down;
    u_long cellular_bw_up;
    u_long cellular_RTT;

    void ntohl() {
        const size_t numints = sizeof(trace_slice) / sizeof(u_long);
        assert(numints*sizeof(u_long) == sizeof(trace_slice)); // no truncation
        for (size_t i = 0; i < numints; ++i) {
            u_long *pos = ((u_long*)this) + i;
            *pos = ::ntohl(*pos);
        }
    }
};

int get_trace(deque<struct trace_slice>& trace)
{
    int sock = socket(PF_INET, SOCK_STREAM, 0);
    handle_error(sock < 0, "Error creating socket to get trace");
    
    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);
    memset(&addr, 0, addrlen);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(EMULATION_BOX_IP);
    addr.sin_port = htons(EMULATION_BOX_PORT);
    
    int rc = connect(sock, (struct sockaddr*)&addr, addrlen);
    handle_error(rc < 0, "Error connecting to get trace");

    int trace_size = 0;
    rc = recv(sock, &trace_size, sizeof(trace_size), MSG_WAITALL);
    handle_error(rc != sizeof(trace_size), "Error receiving trace size");

    trace_size = ntohl(trace_size);
    if (trace_size <= 0) {
        fprintf(stderr, "Received invalid trace size %d\n", trace_size);
        exit(EXIT_FAILURE);
    }
    
    struct timeval trace_start = {-1, 0};
    struct timeval last_slice_start = {0, 0};
    for (int i = 0; i < trace_size; ++i) {
        struct trace_slice slice;
        rc = recv(sock, &slice, sizeof(slice), MSG_WAITALL);
        handle_error(rc != sizeof(slice), "Error receiving trace slice");

        slice.ntohl();
        if (slice.start.tv_sec < 0) {
            fprintf(stderr, "Error: invalid timestamp received with slice\n");
            exit(EXIT_FAILURE);
        }
        if (trace_start.tv_sec == -1) {
            trace_start = slice.start;
            slice.start.tv_sec = slice.start.tv_usec = 0;
        } else {
            timersub(&slice.start, &trace_start, &slice.start);
            
            if (!timercmp(&slice.start, &last_slice_start, >)) {
                fprintf(stderr, "Error: out-of-order timestamps in trace\n");
                exit(EXIT_FAILURE);
            }
            last_slice_start = slice.start;
        }
        trace.push_back(slice);
    }
    
    return sock;
}

static void emulate_slice(struct trace_slice slice, struct timeval end,
                          struct net_interface cellular_iface,
                          struct net_interface wifi_iface);
#define MIN_TIME 0.0

int main(int argc, char *argv[])
{
    if (argc < 4) {
	usage(argv);
    }

    bool trace_replay = false;
    bool sampling = false;
    CDFSampler *up_time_samples = NULL;
    CDFSampler *down_time_samples = NULL;
    double presample_duration = 3600.0;

    char *fg_iface_name = NULL;
    char *bg_iface_name = NULL;

    u_long fg_bandwidth=0, fg_RTT=0, bg_bandwidth=0, bg_RTT=0;

    int argi = 1;
    if (!strcmp(argv[argi], "replay")) {
        trace_replay = true;
        argi++;

        fg_iface_name = argv[argi++];
        bg_iface_name = argv[argi++];
    } else {
        fg_iface_name = argv[argi++];
        fg_bandwidth = atoi(argv[argi++]);
        fg_RTT = atoi(argv[argi++]);
    }

    double up_time = 30.0;
    double down_time = 5.0;
    if (!trace_replay && argc > argi) {
        if ((argc - argi) < 3) {
            usage(argv);
        }

	bg_iface_name = argv[argi++];
        bg_bandwidth = atoi(argv[argi++]);
        bg_RTT = atoi(argv[argi++]);

	if (argc > argi && !strcmp(argv[argi], "cdf")) {
	    if ((argc - argi) < 3) {
		usage(argv);
	    }
            argi++;
	    
	    sampling = true;
	    try {
		auto_ptr<CDFSampler> up_ptr(new CDFSampler(argv[argi++], 
							   presample_duration));
		auto_ptr<CDFSampler> down_ptr(new CDFSampler(argv[argi++],
							     presample_duration));
		
		up_time_samples = up_ptr.release();
		down_time_samples = down_ptr.release();
	    } catch (CDFErr &e) {
		fprintf(stderr, "CDF Error: %s\n", e.str.c_str());
		exit(1);
	    }
	} else {
            if ((argc - argi) < 2) {
                usage(argv);
            }
	    up_time = atof(argv[argi++]);
	    down_time = atof(argv[argi++]);
	    if (up_time < MIN_TIME || down_time < MIN_TIME) {
		fprintf(stderr, 
			"Error: uptime and downtime must be greater than "
			"%f seconds.\n", MIN_TIME);
		exit(-1);
	    }
	}
    }

    signal(SIGINT, handle_term);

    //labels_available = UP_LABELS;
    
    /* Add the interfaces, wizard-of-oz-style */
    struct net_interface ifs[2] = {
        {{0}, CMM_LABEL_ONDEMAND, fg_bandwidth, fg_RTT},
        {{0}, CMM_LABEL_BACKGROUND, bg_bandwidth, bg_RTT}
    };
    const char *ifnames[2] = {fg_iface_name, bg_iface_name};

    size_t num_ifs = 2;
    if (!bg_iface_name) {
	num_ifs = 1;
        ifs[0].labels = 0; // only available interface, so use it for everything
    }

    for (size_t i = 0; i < num_ifs; i++) {
        int rc = get_ip_address(ifnames[i], &ifs[i].ip_addr);
	if (rc < 0) {
	    fprintf(stderr, "blah, couldn't get IP address for %s\n", ifnames[i]);
	    exit(-1);
	}
        net_interfaces[ifs[i].ip_addr.s_addr] = ifs[i];
	printf("Got interface: %s, %s, %lu bytes/sec %lu ms\n", ifnames[i], 
	       inet_ntoa(ifs[i].ip_addr), ifs[i].bandwidth, ifs[i].RTT);
    }
    
    struct net_interface bg_iface;
    IfaceList bg_iface_list;
    IfaceList empty_list;
    
    if (bg_iface_name) {
	bg_iface = ifs[1];

	// will be added back first iteration
	net_interfaces.erase(bg_iface.ip_addr.s_addr);
	bg_iface_list.push_back(bg_iface);
    }

    deque<struct trace_slice> trace;
    int emu_sock = -1;

    if (trace_replay) {
        emu_sock = get_trace(trace);
        assert(!trace.empty());
    }

    running = true;

    pthread_t tid;
    int rc = pthread_create(&tid, NULL, IPC_Listener, NULL);
    if (rc < 0) {
	perror("pthread_create");
	fprintf(stderr, "Couldn't create IPCListener thread, exiting\n");
	exit(-1);
    }

    if (trace_replay) {
        char ch = 0;
        rc = write(emu_sock, &ch, 1);
        handle_error(rc < 0, "Error sending response to emu_box");
        close(emu_sock);
    }

    if (!bg_iface_name) {
	/* no background interface; 
	 * just sleep until SIGINT, then exit */
	(void)select(0, NULL, NULL, NULL, NULL);
	running = false;
    }

    while (running) {
        if (trace_replay) {
            struct trace_slice slice = trace[0];
            trace.pop_front();
            struct timeval end = {-1, 0};
            if (!trace.empty()) {
                end = trace[0].start;
            }

            struct net_interface cellular_iface = ifs[0];
            struct net_interface wifi_iface = ifs[1];
            emulate_slice(slice, end, cellular_iface, wifi_iface);
            continue;
        }

	if (sampling) {
	    up_time = up_time_samples->sample();
	}
	//labels_available = UP_LABELS;
        
	fprintf(stderr, "%s is up for %lf seconds\n", bg_iface_name, up_time);
        pthread_mutex_lock(&ifaces_lock);
        net_interfaces[bg_iface.ip_addr.s_addr] = bg_iface;
        pthread_mutex_unlock(&ifaces_lock);
	notify_all_subscribers(bg_iface_list, empty_list);

	thread_sleep(up_time);
	if (!running) break;

	if (sampling) {
	    down_time = down_time_samples->sample();
	}
	//labels_available = DOWN_LABELS;
	fprintf(stderr, "%s is down for %lf seconds\n", bg_iface_name, down_time);

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

static void emulate_slice(struct trace_slice slice, struct timeval end,
                          struct net_interface cellular_iface,
                          struct net_interface wifi_iface)
{
    IfaceList changed_ifaces, down_ifaces;
    cellular_iface.bandwidth = slice.cellular_bw_up;
    cellular_iface.RTT = slice.cellular_RTT;
    wifi_iface.bandwidth = slice.wifi_bw_up;
    wifi_iface.RTT = slice.wifi_RTT;

    pthread_mutex_lock(&ifaces_lock);
    if (wifi_iface.bandwidth == 0) {
        if (net_interfaces.count(wifi_iface.ip_addr.s_addr) == 1) {
            down_ifaces.push_back(wifi_iface);
        }
    } else {
        net_interfaces[wifi_iface.ip_addr.s_addr] = wifi_iface;
        changed_ifaces.push_back(wifi_iface);
    }
    if (cellular_iface.bandwidth == 0) {
        if (net_interfaces.count(cellular_iface.ip_addr.s_addr) == 1) {
            down_ifaces.push_back(cellular_iface);
        }
    } else {
        net_interfaces[cellular_iface.ip_addr.s_addr] = cellular_iface;
        changed_ifaces.push_back(cellular_iface);
    }
    pthread_mutex_unlock(&ifaces_lock);
    notify_all_subscribers(changed_ifaces, down_ifaces);

    if (end.tv_sec != -1) {
        struct timeval duration;
        TIMEDIFF(slice.start, end, duration);
        thread_sleep(duration);
    } else {
        // all done with the trace, so just sleep until SIGINT
	(void)select(0, NULL, NULL, NULL, NULL);
    }
}
