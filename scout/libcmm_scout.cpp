#include <stdio.h>
#include <pthread.h>
#include <signal.h>
//#include <assert.h>
#include <math.h>
#include <sys/socket.h>
#include <linux/un.h>
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
#include "libcmm_shmem.h"
#include "libcmm_net_preference.h"
#include "pthread_util.h"
#include "common.h"
#include "net_interface.h"
#include <errno.h>

#ifdef ANDROID
#include "network_test.h"
#endif

#ifdef BUILDING_SCOUT_SHLIB
#include <jni.h>
#else
#include "cdf_sampler.h"
#endif

#ifdef ANDROID
#  ifdef NDK_BUILD
#  include <android/log.h>
#  else
#  include <cutils/logd.h>
#  endif
static void DEBUG_LOG(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    __android_log_vprint(ANDROID_LOG_INFO, "ConnScout", fmt, ap);
    va_end(ap);
}
#else
#define DEBUG_LOG dbgprintf_always
#endif

#include "debug.h"

static void LOG_PERROR(const char *str)
{
    DEBUG_LOG("%s: %s\n", str, strerror(errno));
}

#include <memory>
using std::auto_ptr;

struct subscriber_proc {
    int ipc_sock;
    pid_t pid;
};

// map from IPC socket FD to data
typedef LockingMap<int, subscriber_proc> SubscriberProcHash;
static SubscriberProcHash subscriber_procs;

static bool running;
static int emu_sock = -1;

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


static
int notify_subscriber(pid_t pid, int ipc_sock,
                      const IfaceList& changed_ifaces, 
                      const IfaceList& down_ifaces);
static int
notify_subscriber_of_event(pid_t pid, int ipc_sock, 
                           struct net_interface iface, MsgOpcode opcode);

static int init_subscriber(pid_t pid, int ipc_sock)
{
    /* tell the subscriber about all the current interfaces. */
    IfaceList up_ifaces;
    pthread_mutex_lock(&ifaces_lock);
    for (NetInterfaceMap::const_iterator it = net_interfaces.begin();
         it != net_interfaces.end(); it++) {
        up_ifaces.push_back(it->second);
    }
    pthread_mutex_unlock(&ifaces_lock);

    return notify_subscriber(pid, ipc_sock, up_ifaces, IfaceList());
}

/* REQ: ac must be bound to the desired victim by subscriber_procs.find() */
static
void remove_subscriber(SubscriberProcHash::accessor &ac)
{
    close(ac->second.ipc_sock);
    subscriber_procs.erase(ac);    
}

static int scout_control_ipc_sock = -1;

int
make_scout_listener_socket()
{
    int listener_sock = socket(PF_UNIX, SOCK_STREAM, 0);
    if (listener_sock < 0) {
        LOG_PERROR("socket");
        DEBUG_LOG("Failed to open IPC socket\n");
        return -1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(&addr.sun_path[1], SCOUT_CONTROL_MQ_NAME, 
            UNIX_PATH_MAX - 2);
    int rc = bind(listener_sock, (struct sockaddr*)&addr,
                  sizeof(addr));
    if (rc < 0) {
        LOG_PERROR("bind");
        DEBUG_LOG("Failed to bind IPC socket\n");
        close(listener_sock);
        return -1;
    }
    rc = listen(listener_sock, 10);
    if (rc < 0) {
        LOG_PERROR("listen");
        DEBUG_LOG("Failed to listen on IPC socket\n");
        close(listener_sock);
        return -1;
    }
    return listener_sock;
}

#ifdef ANDROID
static struct timeval last_wifi_check = {0, 0};
static struct timeval wifi_check_period = {1, 0};
static int last_wifi_check_result = 1;
#endif

static int
is_network_usable(struct net_interface iface)
{
    if (iface.type == NET_TYPE_WIFI) {
#ifdef ANDROID
        // TODO: switch to UDP ping?
        // TODO: better yet, implement something beacon-based.
        struct in_addr server_ip;
        memset(&server_ip, 0, sizeof(server_ip));
        if (inet_aton("141.212.110.132", &server_ip) < 0) {
            DEBUG_LOG("Failed to decode server IP\n");
            return -1;
        }

        struct timeval now, diff;
        TIME(now);
        TIMEDIFF(last_wifi_check, now, diff);
        if (timercmp(&diff, &wifi_check_period, >=)) {
            TIME(last_wifi_check);
            last_wifi_check_result = is_connected(iface.ip_addr.s_addr, server_ip.s_addr);
        }
        return last_wifi_check_result;
#else
        return 1;
#endif
    } else {
        // don't do anything special for non-wifi networks
        return 1;
    }
}

static
void * IPC_Listener(void *)
{
    int rc;
    scout_control_ipc_sock = make_scout_listener_socket();
    if (scout_control_ipc_sock < 0) {
        raise(SIGINT); /* easy way to bail out */
    }

    //struct timeval timeout = {1, 0}; /* 1-second timeout */
    DEBUG_LOG("IPC thread up and listening.\n");

    fd_set active_fds;
    FD_ZERO(&active_fds);
    FD_SET(scout_control_ipc_sock, &active_fds);
    int maxfd = scout_control_ipc_sock;

    while (running) {
        /* listen for IPCs */
        errno = 0;
        fd_set fds = active_fds;
        int ready_fds = select(maxfd + 1, &fds, NULL, NULL, NULL);//,timeout);
        if (ready_fds == 0) {
            // won't happen unless I enable the timeout
            continue;
        } else if (ready_fds < 0) {
            if (errno == EINTR) {
                continue;
            } else {
                LOG_PERROR("select");
                DEBUG_LOG("Failed to select on IPC socket\n");
#ifndef BUILDING_SCOUT_SHLIB
                // if in JNI shared lib, I'm the only native thread,
                //  so I'll just exit.
                raise(SIGINT);
#endif
                break;
            }
        }

        struct cmm_msg msg;

        if (FD_ISSET(scout_control_ipc_sock, &fds)) {
            // adding new subscriber process
            struct sockaddr_un addr;
            memset(&addr, 0, sizeof(addr));
            socklen_t len = sizeof(addr);
            int ipc_sock = accept(scout_control_ipc_sock, 
                                  (struct sockaddr *)&addr, &len);
            if (ipc_sock < 0) {
                int e = errno;
                LOG_PERROR("accept");
                DEBUG_LOG("Failed accepting new IPC socket\n");
                ready_fds--;
                if (e == EINVAL) {
                    DEBUG_LOG("IPC socket shut down; thread exiting\n");
                    break;
                }
            } else {
                rc = read(ipc_sock, &msg, sizeof(msg));
                if (rc != sizeof(msg)) {
                    if (rc < 0) {
                        LOG_PERROR("read");
                    }
                    DEBUG_LOG("Failed to receive subscribe message: got %d bytes, expected %d\n",
                              rc, (int) sizeof(msg));
                    close(ipc_sock);
                } else {
                    if (msg.opcode == CMM_MSG_GET_IFACES) {
                        // not a real subscriber; just an iface request
                        rc = init_subscriber(msg.pid, ipc_sock);
                        if (rc < 0) {
                            DEBUG_LOG("Failed to send iface info to pid %d\n",
                                      msg.pid);
                        } else {
                            struct net_interface sentinel;
                            memset(&sentinel, 0, sizeof(sentinel));
                            rc = notify_subscriber_of_event(
                                msg.pid, ipc_sock, sentinel,
                                CMM_MSG_IFACE_LABELS
                            );
                            if (rc < 0) {
                                DEBUG_LOG("Failed to send sentinel iface to "
                                          "process %d\n", msg.pid);
                            }
                        }
                        close(ipc_sock);
                    } else if (msg.opcode == CMM_MSG_IS_IP_CONNECTED) {
                        struct net_interface iface;
                        memset(&iface, 0, sizeof(iface));
                        {
                            PthreadScopedLock lock(&ifaces_lock);
                            in_addr_t ip = msg.data.iface.ip_addr.s_addr;
                            if (net_interfaces.count(ip) == 1) {
                                iface = net_interfaces[ip];
                            }
                        }
                        if (iface.ip_addr.s_addr == 0) {
                            DEBUG_LOG("Got is-connected request for unknown iface %s\n",
                                      inet_ntoa(msg.data.iface.ip_addr));
                            msg.rc = -1;
                        } else {
                            DEBUG_LOG("Got is-connected request for iface %s (%s) from process %d\n",
                                      inet_ntoa(iface.ip_addr),
                                      net_type_name(iface.type),
                                      msg.pid);
                            msg.rc = is_network_usable(iface);
                            DEBUG_LOG("Response: iface %s (%s) is %sconnected\n",
                                      inet_ntoa(iface.ip_addr),
                                      net_type_name(iface.type),
                                      msg.rc == 1 ? "" : "not ");
                        }
                        rc = write(ipc_sock, &msg, sizeof(msg));
                        if (rc != sizeof(msg)) {
                            DEBUG_LOG("Failed to send is-connected response to process %d\n",
                                      msg.pid);
                        }
                        close(ipc_sock);
                    } else {
                        SubscriberProcHash::accessor ac;
                        if (subscriber_procs.find(ac, msg.pid)) {
                            DEBUG_LOG("Duplicate subscribe request received "
                                "from process %d\n", msg.pid);
                            DEBUG_LOG("Reopening socket\n");
                            close(ac->second.ipc_sock);
                        } else {
                            subscriber_procs.insert(ac, ipc_sock);
                            ac->second.pid = msg.pid;
                        }
                        
                        rc = init_subscriber(msg.pid, ipc_sock);
                        if (rc < 0) {
                            close(ipc_sock);
                            subscriber_procs.erase(ac);
                        } else {
                            ac->second.ipc_sock = ipc_sock;
                            DEBUG_LOG("Process %d subscribed\n", 
                                    msg.pid);
                            if (ipc_sock > maxfd) {
                                maxfd = ipc_sock;
                            }
                            FD_SET(ipc_sock, &active_fds);
                        }
                    }
                    
                    ready_fds--;
                    if (ready_fds == 0) {
                        continue;
                    }
                }
            }
        }

        for (int s = 0; s <= maxfd; ++s) {
            if (ready_fds == 0) {
                break;
            }

            if (FD_ISSET(s, &fds)) {
                SubscriberProcHash::accessor ac;
                if (subscriber_procs.find(ac, s)) {
                    rc = read(s, &msg, sizeof(msg));
                    if (rc != sizeof(msg)) {
                        if (rc < 0) {
                            DEBUG_LOG("read: %s\n", strerror(errno));
                        }
                        DEBUG_LOG("Hmm... process %d must have died\n",
                                ac->second.pid);
                    } else {
                        DEBUG_LOG("Process %d unsubscribed\n", msg.pid);
                    }
                    remove_subscriber(ac);
                    FD_CLR(s, &active_fds);
                } else {
                    // never happens; the socket wouldn't be in the fd_set
                    ASSERT(0);
                }
                ready_fds--;
            }
        }
    }
    
    // close all IPC sockets
    for (SubscriberProcHash::iterator it = subscriber_procs.begin();
         it != subscriber_procs.end(); it++) {
        close(it->second.ipc_sock);
    }
    close(scout_control_ipc_sock);
    return NULL;
}

static
int notify_subscriber_of_event(pid_t pid, int ipc_sock, 
                               struct net_interface iface, MsgOpcode opcode)
{
    struct cmm_msg msg;
    msg.opcode = opcode;
    msg.data.iface = iface;
    int rc = write(ipc_sock, (char*)&msg, sizeof(msg));
    if (rc != sizeof(msg)) {
        if (rc < 0) {
            LOG_PERROR("write");
        }
        DEBUG_LOG("Failed to notify subscriber proc %d\n", pid);
    }
    return rc;
}

static
int notify_subscriber(pid_t pid, int ipc_sock,
                      const IfaceList& changed_ifaces, 
                      const IfaceList& down_ifaces)
{
    int rc;
    for (size_t i = 0; i < changed_ifaces.size(); i++) {
        const struct net_interface& iface = changed_ifaces[i];
        rc = notify_subscriber_of_event(pid, ipc_sock, iface, 
                                        CMM_MSG_IFACE_LABELS);
        if (rc < 0) {
            return rc;
        }
    }
    for (size_t i = 0; i < down_ifaces.size(); i++) {
        const struct net_interface& iface = down_ifaces[i];
        rc = notify_subscriber_of_event(pid, ipc_sock, iface, 
                                        CMM_MSG_IFACE_DOWN);
        if (rc < 0) {
            return rc;
        }
    }

    return 0;
}

static
void notify_all_subscribers(const IfaceList& changed_ifaces,
                            const IfaceList& down_ifaces)
{
    int rc;
    queue<pid_t> failed_procs;
 
    for (SubscriberProcHash::iterator it = subscriber_procs.begin();
         it != subscriber_procs.end(); it++) {
        rc = notify_subscriber(it->first, it->second.ipc_sock,
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

#ifndef BUILDING_SCOUT_SHLIB
static
void handle_term(int)
{
    dbgprintf_always("Scout attempting to quit gracefully...\n");
    running = false;
    shutdown(emu_sock, SHUT_RDWR);
}
#endif

static
void usage(char *argv[])
{
    dbgprintf_always(
            "Usage: conn_scout <FG iface> <bandwidth> <RTT>\n"
            "                  [<BG iface> <bandwidth> <RTT>\n"
            "                   [uptime downtime]]\n");
    dbgprintf_always(
            "Usage:    uptime, downtime are in seconds;\n"
            "          bandwidth=bytes/sec, RTT=ms.\n");
    dbgprintf_always(
            "Usage 2: conn_scout <FG iface> <bandwidth> <RTT>\n"
            "                    <BG iface> <bandwidth> <RTT>\n"
            "                    cdf <encounter duration cdf file>\n"
            "                        <disconnect duration cdf file>\n");
    dbgprintf_always(
            "Usage 3: conn_scout replay <FG iface> <BG iface>\n"
            "   -Connects to the emulation box, which will transmit\n"
            "     the trace of network measurements.\n");
    dbgprintf_always("Usage 4: conn_scout socket_control \n"
                     "                    <FG iface> <bandwidth> <RTT>\n"
                     "                    <BG iface> <bandwidth> <RTT>\n"
                     "  -Acts upon up/down commands from a socket.\n");
    exit(-1);
}

static
void thread_sleep(struct timeval tv);

void thread_sleep(double fseconds)
{
    double iseconds = -1.0;
    double fnseconds = modf(fseconds, &iseconds);
    struct timeval timeout;
    timeout.tv_sec = (time_t)iseconds;
    timeout.tv_usec = (long)(fnseconds*1000000);

    if (fseconds <= 0.0) {
        dbgprintf_always("Error: fseconds <= 0.0 (or just too big!)\n");
        raise(SIGINT);
    }

    thread_sleep(timeout);
}

static
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

static
int get_ip_address(const char *ifname, struct in_addr *ip_addr)
{
    if (strlen(ifname) > IF_NAMESIZE) {
        dbgprintf_always("Error: ifname too long (longer than %d)\n", IF_NAMESIZE);
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
    long gps_lat; // coord to 0.001, times 1000
    long gps_long; // coord to 0.001, times 1000
    long wifi_rssi;
    char wifi_ssid[32]; // maximum ssid length

    void ntohl_all() {
        const size_t numints = (sizeof(trace_slice) - 32) / sizeof(u_long);
        ASSERT(numints*sizeof(u_long) == (sizeof(trace_slice) - 32)); // no truncation
        for (size_t i = 0; i < numints; ++i) {
            u_long *pos = ((u_long*)this) + i;
            *pos = ntohl(*pos);
        }
    }
};

static
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
        dbgprintf_always("Received invalid trace size %d\n", trace_size);
        exit(EXIT_FAILURE);
    }
    
    struct timeval trace_start = {-1, 0};
    struct timeval last_slice_start = {0, 0};
    for (int i = 0; i < trace_size; ++i) {
        struct trace_slice slice;
        rc = recv(sock, &slice, sizeof(slice), MSG_WAITALL);
        handle_error(rc != sizeof(slice), "Error receiving trace slice");

        slice.ntohl_all();
        if (slice.start.tv_sec < 0) {
            dbgprintf_always("Error: invalid timestamp received with slice\n");
            exit(EXIT_FAILURE);
        }
        if (trace_start.tv_sec == -1) {
            trace_start = slice.start;
            slice.start.tv_sec = slice.start.tv_usec = 0;
        } else {
            timersub(&slice.start, &trace_start, &slice.start);
            
            if (!timercmp(&slice.start, &last_slice_start, >)) {
                dbgprintf_always("Error: out-of-order timestamps in trace\n");
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
                          struct net_interface wifi_iface,
                          int emu_sock);
#define MIN_TIME 0.0

#ifdef BUILDING_SCOUT_SHLIB
extern "C"
jint 
Java_edu_umich_intnw_scout_ConnScoutService_startScoutIPC(JNIEnv *env, 
                                                          jobject thiz)
{
    if (!running) {
        //set_signal(SIGINT, handle_term);
        running = true;
        
        pthread_t tid;
        int rc = pthread_create(&tid, NULL, IPC_Listener, NULL);
        if (rc != 0) {
            LOG_PERROR("pthread_create");
            DEBUG_LOG("Couldn't create IPCListener thread, exiting\n");
            return rc;
        }
    }
}

extern "C"
void 
Java_edu_umich_intnw_scout_ConnScoutService_stopScoutIPC(JNIEnv *env, 
                                                         jobject thiz)
{
    DEBUG_LOG("Scout attempting to quit gracefully...\n");
    running = false;
    shutdown(scout_control_ipc_sock, SHUT_RDWR);
    // IPC thread will then exit.
    
    PthreadScopedLock lock(&ifaces_lock);
    net_interfaces.clear();
}

extern "C"
void 
Java_edu_umich_intnw_scout_ConnScoutService_updateNetwork(JNIEnv *env, 
                                                          jobject thiz,
                                                          jstring ip_addr, 
                                                          jint bw_down, 
                                                          jint bw_up, 
                                                          jint rtt,
                                                          jboolean down,
                                                          jint networkType)
{
    struct net_interface iface;
    memset(&iface, 0, sizeof(iface));
    iface.bandwidth_down = (u_long)bw_down;
    iface.bandwidth_up = (u_long)bw_up;
    iface.RTT = (u_long)rtt;
    iface.type = networkType;
    const char *str = env->GetStringUTFChars(ip_addr, NULL);
    if (str == NULL) {
        DEBUG_LOG("Got null IP address string in updateNetwork!\n");
        return;
    }

    int rc = inet_aton(str, &iface.ip_addr);
    if (rc != 1) {
        DEBUG_LOG("Invalid IP address string: %s\n", str);
        env->ReleaseStringUTFChars(ip_addr, str);
        return;
    }
    
    IfaceList changed_ifaces;
    IfaceList empty_list;
    
    pthread_mutex_lock(&ifaces_lock);
    if (down) {
        DEBUG_LOG("updateNetwork: bringing down iface %s type %s\n", 
                  str, net_type_name(networkType));
        net_interfaces.erase(iface.ip_addr.s_addr);
        changed_ifaces.push_back(iface);
    } else {
        DEBUG_LOG("updateNetwork: updating iface %s bw_down %d bw_up %d rtt %d type %d (%s)\n",
                  str, bw_down, bw_up, rtt, networkType, net_type_name(networkType));
        net_interfaces[iface.ip_addr.s_addr] = iface;
        changed_ifaces.push_back(iface);
    }
    pthread_mutex_unlock(&ifaces_lock);
    notify_all_subscribers(down ? empty_list : changed_ifaces,
                           down ? changed_ifaces : empty_list);
    
    env->ReleaseStringUTFChars(ip_addr, str);
}
#else /* !BUILDING_SCOUT_SHLIB */

void put_up_iface(struct net_interface iface)
{
}

int main(int argc, char *argv[])
{
#ifndef ANDROID
    // ensure shared memory gets cleaned up when I exit
    struct shmem_remover {
        ~shmem_remover() { ipc_shmem_deinit(); }
    } remover;
    (void)remover; // suppress "unused variable" warning

    ipc_shmem_init(true);
#endif

    if (argc < 4) {
        usage(argv);
    }

    signal(SIGPIPE, SIG_IGN);

    bool trace_replay = false;
    bool read_socket = false;
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
    } else if (!strcmp(argv[argi], "socket_control")) {
        read_socket = true;
        argi++;

        fg_iface_name = argv[argi++];
        fg_bandwidth = atoi(argv[argi++]);
        fg_RTT = atoi(argv[argi++]);
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
                dbgprintf_always("CDF Error: %s\n", e.str.c_str());
                exit(1);
            }
        } else if (read_socket) {
            // nothing; don't read up/down time because it's socket-driven
        } else {
            if ((argc - argi) < 2) {
                usage(argv);
            }
            up_time = atof(argv[argi++]);
            down_time = atof(argv[argi++]);
            if (up_time < MIN_TIME || down_time < MIN_TIME) {
                dbgprintf_always(
                        "Error: uptime and downtime must be greater than "
                        "%f seconds.\n", MIN_TIME);
                exit(-1);
            }
        }
    }

    set_signal(SIGINT, handle_term);

    //labels_available = UP_LABELS;
    
    /* Add the interfaces, wizard-of-oz-style */
    struct net_interface ifs[2] = {
        {{0}, CMM_LABEL_ONDEMAND, fg_bandwidth, fg_bandwidth,  fg_RTT, 0},
        {{0}, CMM_LABEL_BACKGROUND, bg_bandwidth, bg_bandwidth, bg_RTT, 0}
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
            dbgprintf_always("blah, couldn't get IP address for %s\n", ifnames[i]);
            exit(-1);
        }
        net_interfaces[ifs[i].ip_addr.s_addr] = ifs[i];
        printf("Got interface: %s, %s, %lu bytes/sec %lu ms\n", ifnames[i], 
               inet_ntoa(ifs[i].ip_addr), ifs[i].bandwidth_up, ifs[i].RTT);
    }
    
    struct net_interface bg_iface;
    IfaceList bg_iface_list;
    IfaceList empty_list;
    
    if (bg_iface_name) {
        bg_iface = ifs[1];

        bg_iface_list.push_back(bg_iface);
        if (!read_socket) {
            // will be added back first iteration
            net_interfaces.erase(bg_iface.ip_addr.s_addr);
        }
    }

    deque<struct trace_slice> trace;

    if (trace_replay) {
        emu_sock = get_trace(trace);
        ASSERT(!trace.empty());
    }

    running = true;

    pthread_t tid;
    int rc = pthread_create(&tid, NULL, IPC_Listener, NULL);
    if (rc < 0) {
        perror("pthread_create");
        dbgprintf_always("Couldn't create IPCListener thread, exiting\n");
        exit(-1);
    }

    if (trace_replay) {
        dbgprintf_always("Starting trace replay\n");
        /*
        for (int i = 3; i > 0; --i) {
            dbgprintf_always("%d..", i);
            sleep(1);
        }
        dbgprintf_always("\n");
        */

        char ch = 0;
        rc = write(emu_sock, &ch, 1);
        handle_error(rc < 0, "Error sending response to emu_box");
        //close(emu_sock);
    }

    if (!bg_iface_name) {
        /* no background interface; 
         * just sleep until SIGINT, then exit */
        (void)select(0, NULL, NULL, NULL, NULL);
        running = false;
    }

    // for control-socket driving
    int control_sock = -1;
    FILE *control_socket_file = NULL;

    int control_listen_sock = socket(PF_INET, SOCK_STREAM, 0);
    handle_error(control_listen_sock < 0, "creating down/up control socket");

    if (read_socket) {
        int on = 1;
        int rc = setsockopt(control_listen_sock, SOL_SOCKET, SO_REUSEADDR,
                            (char *) &on, sizeof(on));
        if (rc < 0) {
            DEBUG_LOG("Cannot reuse socket address\n");
        }

        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons(CONTROL_SOCKET_PORT);
        socklen_t addrlen = sizeof(addr);
        rc = bind(control_listen_sock, (struct sockaddr *) &addr, addrlen);
        handle_error(rc < 0, "binding down/up control socket");
        
        rc = listen(control_listen_sock, 5);
        handle_error(rc < 0, "listening on down/up control socket");
    }

    size_t cur_trace_slice = 0;
    while (running) {
        if (trace_replay) {
            if (cur_trace_slice + 1 == trace.size()) {
                dbgprintf_always("Looping the trace\n");
                cur_trace_slice = 0;
                continue;
            }
            struct trace_slice slice = trace[cur_trace_slice++];
            ASSERT(cur_trace_slice < trace.size());
            struct timeval end = trace[cur_trace_slice].start;

            struct net_interface cellular_iface = ifs[0];
            struct net_interface wifi_iface = ifs[1];
            emulate_slice(slice, end, cellular_iface, wifi_iface, emu_sock);
            continue;
        } else if (read_socket) {
            if (control_socket_file == NULL) {
                control_sock = accept(control_listen_sock, NULL, NULL);
                if (control_sock < 0) {
                    if (errno == EINTR) {
                        // should fall out of the loop
                        continue;
                    } else {
                        handle_error(control_sock < 0, "accepting connection on control socket");
                    }
                }
                control_socket_file = fdopen(control_sock, "r");
                handle_error(control_socket_file == NULL, "fdopen");

                dbgprintf_always("connection on control socket\n");
            }

            const size_t TOKEN_LEN = 80;
            char cmd[TOKEN_LEN + 1];
            int rc = fscanf(control_socket_file, "%80s", cmd);
            if (rc != 1) {
                if (feof(control_socket_file) || ferror(control_socket_file)) {
                    dbgprintf_always("No more commands on socket\n");
                    fclose(control_socket_file);
                    close(control_sock);
                    control_socket_file = NULL;
                    control_sock = -1;
                } else {
                    dbgprintf_always("Error: malformed input on socket\n");
                }
            } else {
                if (!strcmp(cmd, "bg_down")) {
                  dbgprintf_always("Got bg_down command; bringing down %s\n",
                                   inet_ntoa(bg_iface.ip_addr));
                    pthread_mutex_lock(&ifaces_lock);
                    net_interfaces.erase(bg_iface.ip_addr.s_addr);
                    pthread_mutex_unlock(&ifaces_lock);
                    notify_all_subscribers(empty_list, bg_iface_list);
                } else if (!strcmp(cmd, "bg_up")) {
                  dbgprintf_always("Got bg_up command; bringing up %s\n",
                                   inet_ntoa(bg_iface.ip_addr));
                    pthread_mutex_lock(&ifaces_lock);
                    net_interfaces[bg_iface.ip_addr.s_addr] = bg_iface;
                    pthread_mutex_unlock(&ifaces_lock);
                    notify_all_subscribers(bg_iface_list, empty_list);
                } else { 
                    dbgprintf_always("Error: unrecognized command '%s' on socket\n", cmd);
                }
            }

            continue;
        }

        if (sampling) {
            up_time = up_time_samples->sample();
        }
        //labels_available = UP_LABELS;
        
        dbgprintf_always("%s is up for %lf seconds\n", bg_iface_name, up_time);
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
        dbgprintf_always("%s is down for %lf seconds\n", bg_iface_name, down_time);

        pthread_mutex_lock(&ifaces_lock);
        net_interfaces.erase(bg_iface.ip_addr.s_addr);
        pthread_mutex_unlock(&ifaces_lock);
        notify_all_subscribers(empty_list, bg_iface_list);

        thread_sleep(down_time);
    }

    delete up_time_samples;
    delete down_time_samples;

    shutdown(scout_control_ipc_sock, SHUT_RDWR);
    pthread_join(tid, NULL);
    close(emu_sock);
    dbgprintf_always("Scout gracefully quit.\n");
    return 0;
}
#endif /* ifdef BUILDING_SCOUT_SHLIB */

static void print_slice(struct trace_slice slice, struct timeval end,
                        struct net_interface cellular_iface,
                        struct net_interface wifi_iface)
{
#ifdef ANDROID
    FILE *fp = stdout;
#else
    FILE *fp = stderr;
#endif
    fprintf(fp, "Trace slice  start %lu.%06lu  end ",
            slice.start.tv_sec, slice.start.tv_usec);
    if (end.tv_sec != -1) {
        fprintf(fp, "%lu.%06lu\n", end.tv_sec, end.tv_usec);
    } else {
        fprintf(fp, " (never)\n");
    }
    fprintf(fp, "  Cellular: %15s %9lu down  %9lu up  %5lu ms RTT\n",
            inet_ntoa(cellular_iface.ip_addr),
            slice.cellular_bw_down, slice.cellular_bw_up,
            slice.cellular_RTT);

    fprintf(fp, "  WiFi:     %15s ",
            inet_ntoa(wifi_iface.ip_addr));
    if (wifi_iface.bandwidth_up == 0) {
        fprintf(fp, "(unavailable)\n");
    } else {
        fprintf(fp, "%9lu down  %9lu up  %5lu ms RTT\n",
                slice.wifi_bw_down, slice.wifi_bw_up,
                slice.wifi_RTT);
    }    
}

static void emulate_slice(struct trace_slice slice, struct timeval end,
                          struct net_interface cellular_iface,
                          struct net_interface wifi_iface,
                          int emu_sock)
{
    struct timeval emu_slice_start = {-1, 0};
    int rc = recv(emu_sock, &emu_slice_start, sizeof(emu_slice_start),
                  MSG_WAITALL);
    if (rc != sizeof(emu_slice_start)) {
        if (running) {
            handle_error(rc != sizeof(emu_slice_start), "recv");
        } else {
            /* exiting due to SIGINT */
            return;
        }
    }

    emu_slice_start.tv_sec = ntohl(emu_slice_start.tv_sec);
    emu_slice_start.tv_usec = ntohl(emu_slice_start.tv_usec);
    if (!timercmp(&emu_slice_start, &slice.start, ==)) {
        dbgprintf_always("slice.start=%lu.%06lu, but "
                "emubox says it's %lu.%06lu; exiting\n",
                slice.start.tv_sec, slice.start.tv_usec, 
                emu_slice_start.tv_sec, emu_slice_start.tv_usec);
        running = false;
        return;
    }

    IfaceList changed_ifaces, down_ifaces;
    cellular_iface.bandwidth_down = slice.cellular_bw_down;
    cellular_iface.bandwidth_up = slice.cellular_bw_up;
    cellular_iface.RTT = slice.cellular_RTT;
    wifi_iface.bandwidth_down = slice.wifi_bw_down;
    wifi_iface.bandwidth_up = slice.wifi_bw_up;
    wifi_iface.RTT = slice.wifi_RTT;

    print_slice(slice, end, cellular_iface, wifi_iface);

    pthread_mutex_lock(&ifaces_lock);
    if (wifi_iface.bandwidth_up == 0) {
        if (net_interfaces.count(wifi_iface.ip_addr.s_addr) == 1) {
            down_ifaces.push_back(wifi_iface);
            net_interfaces.erase(wifi_iface.ip_addr.s_addr);
        }
    } else {
        net_interfaces[wifi_iface.ip_addr.s_addr] = wifi_iface;
        changed_ifaces.push_back(wifi_iface);
    }
    /*
    if (cellular_iface.bandwidth_up == 0) {
        if (net_interfaces.count(cellular_iface.ip_addr.s_addr) == 1) {
            down_ifaces.push_back(cellular_iface);
        }
    } else {
    */
    // if cellular_iface.bandwidth_up == 0, it could be
    //  a temporary situation, so instead of telling apps
    //  that the iface is gone, just tell them that it's
    //  really slow.  That way, connections can continue uninterrupted.
    net_interfaces[cellular_iface.ip_addr.s_addr] = cellular_iface;
    changed_ifaces.push_back(cellular_iface);
    //}
    pthread_mutex_unlock(&ifaces_lock);
    notify_all_subscribers(changed_ifaces, down_ifaces);

    if (end.tv_sec != -1) {
        /*
        struct timeval duration;
        TIMEDIFF(slice.start, end, duration);
        thread_sleep(duration);
        */
        //struct timeval next_slice_start = {-1, 0};
        
    } else {
        // all done with the trace, so just sleep until SIGINT
        (void)select(0, NULL, NULL, NULL, NULL);
    }
}
