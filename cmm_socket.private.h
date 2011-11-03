#ifndef cmm_socket_private_h
#define cmm_socket_private_h

#include "cmm_socket.h"
//#include "csocket.h"
#include "common.h"
#include "net_interface.h"

#include "pending_irob.h"
#include "pending_receiver_irob.h"
#include "irob_scheduling.h"

#include <map>
#include <vector>
#include <set>
#include <boost/shared_ptr.hpp>
#include <time.h>

#include <arpa/inet.h>

struct BlockingRequest;
struct ResumeOperation;

/* only used internally, to mark an IROB that
   is waiting for a suitable network, but has no thunk. 
   Shouldn't really happen, but a future label could conceivably 
   necessitate this behavior. */
#define CMM_BLOCKING -3

class CMMSocketImpl;
typedef boost::shared_ptr<CMMSocketImpl> CMMSocketImplPtr;

class CSocket;
class CSockMapping;
//#include "csocket_mapping.h"

struct sockopt {
    void *optval;
    socklen_t optlen;

    sockopt() : optval(NULL), optlen(0) {}
};

/* < optname, (optval, optlen) > */
typedef std::map<int, struct sockopt> SockOptNames;

/* < level, < optname, (optval, optlen) > > */
typedef std::map<int, SockOptNames> SockOptHash;


#include "pthread_util.h"
typedef LockWrappedMap<mc_socket_t, CMMSocketImplPtr> CMMSockHash;

typedef LockingMap<irob_id_t, mc_socket_t> IROBSockHash;

typedef LockingMap<int, void*> VanillaListenerSet;

class ConnBootstrapper;
class ListenerThread;
class CMMSocketSender;
class CMMSocketReceiver;

class CMMSocketImpl : public CMMSocket {
  public:
    static mc_socket_t create(int family, int type, int protocol);
    static CMMSocketPtr lookup(mc_socket_t sock);
    static CMMSocketPtr lookup_by_irob(irob_id_t id);
    static int mc_close(mc_socket_t sock);

    static void interface_down(struct net_interface down_iface);
    static void interface_up(struct net_interface up_iface);

    //virtual int check_label(u_long label, resume_handler_t fn, void *arg);

    virtual int mc_connect(const struct sockaddr *serv_addr,
                           socklen_t addrlen);
    virtual ssize_t mc_send(const void *buf, size_t len, int flags,
                            u_long send_labels, 
                            resume_handler_t resume_handler, void *arg);
    virtual int mc_writev(const struct iovec *vec, int count,
                          u_long send_labels, 
                          resume_handler_t resume_handler, void *arg);
    virtual int mc_shutdown(int how);

    virtual irob_id_t mc_begin_irob(int numdeps, const irob_id_t *deps, 
                                    u_long send_labels, 
                                    resume_handler_t rh, void *rh_arg);
    virtual int mc_end_irob(irob_id_t id);
    virtual ssize_t mc_irob_send(irob_id_t id, 
                                 const void *buf, size_t len, int flags);
    virtual int mc_irob_writev(irob_id_t id, 
                               const struct iovec *vector, int count);
    virtual int irob_relabel(irob_id_t id, u_long new_labels);

    static int mc_select(mc_socket_t nfds, 
                         fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
                         struct timeval *timeout);
    static int mc_poll(struct pollfd fds[], nfds_t nfds, int timeout);

    static int mc_listen(int listener_sock, int backlog);
    static mc_socket_t mc_accept(int listener_sock, 
                                 struct sockaddr *addr, socklen_t *addrlen);

    virtual int mc_recv(void *buf, size_t count, int flags,
                        u_long *recv_labels);

    virtual int mc_getpeername(struct sockaddr *address, 
                               socklen_t *address_len);
    virtual int mc_getsockname(struct sockaddr *address, 
                               socklen_t *address_len);
    //virtual int reset();
    virtual int mc_getsockopt(int level, int optname, 
                              void *optval, socklen_t *optlen);
    virtual int mc_setsockopt(int level, int optname, 
                              const void *optval, socklen_t optlen);

    virtual int mc_get_failure_timeout(u_long label, struct timespec *ts);
    virtual int mc_set_failure_timeout(u_long label, const struct timespec *ts);
    
    virtual ~CMMSocketImpl();

    static bool net_available(mc_socket_t sock, 
                              u_long send_labels);
    
    void add_connection(int sock, 
                        struct in_addr local_addr,
                        struct net_interface remote_iface);

    // cmm_close all remaining mc_sockets.
    static void cleanup();

    // for testing only.
    void drop_irob_and_dependents(irob_id_t irob);
    
  private:
    // XXX: WHAT.  this is kind of silly.
    // TODO: refactor the boundaries between these classes
    // TODO:  to obviate the need for all these, and/or
    // TODO:  figure out which ones are already unneeded.
    friend class CSocket;
    friend class CSockMapping;
    friend class CSocketSender;
    friend class CSocketReceiver;
    friend class ListenerThread;
    friend class ConnBootstrapper;
    friend class PendingReceiverIROBLattice;

    static pthread_mutex_t hashmaps_mutex;
    static CMMSockHash cmm_sock_hash;
    static IROBSockHash irob_sock_hash;
    static irob_id_t g_next_irob;

    static VanillaListenerSet cmm_listeners;
    static NetInterfaceSet ifaces;

    RWLOCK_T my_lock;

    virtual void setup(struct net_interface iface, bool local);
    virtual void teardown(struct net_interface iface, bool local);

    void data_check_all_irobs(in_addr_t local_ip=0, in_addr_t remote_ip=0, 
                              u_long label_mask=0);
    
    int set_all_sockopts(int osfd);

    void send_hello(int bootstrap_sock);
    int recv_hello(int bootstrap_sock); // returns the number of remote ifaces.

    // returns true if it received the "sentinel" iface.
    bool recv_remote_listener(int bootstrap_sock);
    void recv_remote_listeners(int bootstrap_sock, int num_ifaces);
    void send_local_listener(int bootstrap_sock, struct net_interface iface);
    void send_local_listeners(int bootstrap_sock);

    // create csockets for all the interface pairs.
    void startup_csocks();

    // wait for the csockets to connect.
    // only called on bootstrapping, like startup_csocks().
    void wait_for_connections();

    int connection_bootstrap(const struct sockaddr *remote_addr, 
                             socklen_t addrlen,
                             int bootstrap_sock = -1);

    int connect_status();

    bool accepting_side; // true iff created by cmm_accept
    ConnBootstrapper *bootstrapper;

    CMMSocketImpl(int family, int type, int protocol);

    mc_socket_t sock; /* file-descriptor handle for this multi-socket */

    // actually created by socketpair() now, so that I can use shutdown.
    int select_pipe[2]; /* pipe for waking up read-selects */
    int write_ready_pipe[2]; /* pipe for waking up write-selects */

    // empty the select pipe so that the next select/poll
    // will check the incoming_irobs data structure
    void clear_select_pipe(int fd, bool already_locked = false);


    CSockMapping *csock_map;

    //static struct timeval total_inter_fg_time; // time between fg actions
    //static size_t fg_count;
    //static bool okay_to_send_bg(struct timeval& time_since_last_fg);
    //static struct timeval bg_wait_time();
    struct timeval last_fg; // the time of the last foreground activity.
    void update_last_fg();

    NetInterfaceSet local_ifaces;
    ListenerThread *listener_thread;

    /* these are used for connecting csockets */
    NetInterfaceSet remote_ifaces;
    in_port_t remote_listener_port; /* network byte order, 
                                     * recv'd from remote host */

    // returns true if this multisocket is loopback-based - that is,
    //   two endpoints connected to loopback on the same machine.
    // If locked==true, read-lock the multisocket first.
    bool isLoopbackOnly(bool locked=true);

    // Functions to manipulate IROB data structures
    // and other data that the network threads are monitoring
    int begin_irob(irob_id_t next_irob, 
                   int numdeps, const irob_id_t *deps,
                   u_long send_labels, 
                   resume_handler_t resume_handler, void *rh_arg);
    int end_irob(irob_id_t id);
    long irob_chunk(irob_id_t, const void *buf, size_t len, int flags);

    int default_irob(irob_id_t next_irob, 
                     const void *buf, size_t len, int flags,
                     u_long send_labels, 
                     resume_handler_t resume_handler, void *arg);
    int default_irob_writev(irob_id_t next_irob, 
                            const struct iovec *vec, int count, 
                            ssize_t total_bytes,
                            u_long send_labels,
                            resume_handler_t resume_handler, void *rh_arg);
    int validate_default_irob(u_long send_labels,
                              resume_handler_t resume_handler, void *rh_arg,
                              CSocket *& csock);
    int send_default_irob(irob_id_t id, CSocket *csock,
                          char *buf, size_t len,
                          u_long send_labels,
                          resume_handler_t resume_handler, void *rh_arg);

    void new_interface(struct in_addr ip_addr, u_long labels);
    void down_interface(struct in_addr ip_addr);
    void ack(irob_id_t id, u_long seqno, 
             u_long ack_send_labels);
    void goodbye(bool remote_initiated);
    
    /* These are called by the receiver when their associated messages
     * are received. */
    void ack_received(irob_id_t id);
    void goodbye_acked(void);
    void resend_request_received(irob_id_t id, resend_request_type_t request,
                                 u_long seqno, int next_chunk);//, size_t offset, size_t len);
    void data_check_requested(irob_id_t id);
    
    bool is_shutting_down(void);

#define CMM_INVALID_RC -10

    struct AppThread {
        pthread_mutex_t mutex;
        pthread_cond_t cv;
        long rc;
        
        AppThread() : rc(CMM_INVALID_RC) {
            pthread_mutex_init(&mutex, NULL);
            pthread_cond_init(&cv, NULL);
        }
        ~AppThread() {
            pthread_mutex_destroy(&mutex);
            pthread_cond_destroy(&cv);
        }
    };
    
    // For blocking and waking up application threads as needed
    std::map<pthread_t, AppThread> app_threads;

    // always call in app thread before wait_for_completion, before
    // modifying the state that will trigger the desired operation
    void prepare_app_operation();

    // assumes prepare_app_operation has been called in this thread
    // with no call to wait_for_completion since.
    // optional labels indicate which labels to wait for
    // if this wait should timeout.
    long wait_for_completion(u_long labels = 0);

    // called from sender-scheduler thread to wake up app thread
    void signal_completion(pthread_t requester_tid, long result);

    friend void unblock_thread_thunk(BlockingRequest *req);
    friend void resume_operation_thunk(ResumeOperation *op);

    // maps labels to failure timeoutus.
    std::map<u_long, struct timespec> failure_timeouts;

    int wait_for_labels(u_long send_labels);

    int get_csock(u_long send_labels, 
                  resume_handler_t resume_handler, void *rh_arg,
                  CSocket *& csock, bool blocking);

    void remove_if_unneeded(PendingIROBPtr pirob);

    /* true iff the socket has begun shutting down 
     * via shutdown() or close(). */
    bool shutting_down;
    bool remote_shutdown; /* true if remote has ack'd the shutdown */
    bool goodbye_sent;
    
    // for protecting data structures that comprise the "state"
    //  of the multisocket from the scheduling threads' perspective
    // It also protects CSocket-specific state; e.g. 
    //  the index of IROBs with a specific label.
    pthread_mutex_t scheduling_state_lock;
    pthread_cond_t scheduling_state_cv;

    // the newest status of these has not been sent to the remote side
    NetInterfaceSet changed_local_ifaces;
    NetInterfaceSet down_local_ifaces;

    PendingIROBLattice outgoing_irobs;
    PendingReceiverIROBLattice incoming_irobs;

    // unlabeled IROB actions; can be picked up by any csocket
    IROBSchedulingIndexes irob_indexes;
    bool sending_goodbye;

    bool non_blocking;
    bool is_non_blocking(); // considers calls to fcntl with O_NONBLOCK

    /* these are used for creating new physical sockets */
    int sock_family;
    int sock_type;
    int sock_protocol;
    SockOptHash sockopts;

    void get_fds_for_select(mcSocketOsfdPairList &osfd_list, 
                            bool reading, bool writing);
    static int make_real_fd_set(int nfds, fd_set *fds,
                                mcSocketOsfdPairList &osfd_list, 
                                int *maxosfd, 
                                bool reading, bool writing);
    static int make_mc_fd_set(fd_set *fds, 
                              const mcSocketOsfdPairList &osfd_list);

    bool net_available(u_long send_labels);

    class static_destroyer {
      public:
        ~static_destroyer();
    };
    static static_destroyer destroyer;

    struct NetRestrictionStats {
        size_t bytes_sent;
        size_t bytes_recvd;
        NetRestrictionStats() : bytes_sent(0), bytes_recvd(0) {}
    };
    std::map<int, NetRestrictionStats> net_restriction_stats;

    // must hold scheduling_state_lock
    void update_net_restriction_stats(int labels, size_t bytes_sent, size_t bytes_recvd);
};

class CMMSocketPassThrough : public CMMSocket {
  public:
    CMMSocketPassThrough(mc_socket_t sock_);

    virtual int mc_getpeername(struct sockaddr *address, 
                               socklen_t *address_len);
    virtual int mc_getsockname(struct sockaddr *address, 
                               socklen_t *address_len);
    //virtual int reset();
    //virtual int check_label(u_long label, resume_handler_t fn, void *arg);
    virtual int mc_recv(void *buf, size_t count, int flags,
                        u_long *recv_labels);
    virtual int mc_getsockopt(int level, int optname, 
                              void *optval, socklen_t *optlen);
    virtual int mc_setsockopt(int level, int optname, 
                              const void *optval, socklen_t optlen);

    virtual int mc_connect(const struct sockaddr *serv_addr,
                           socklen_t addrlen);
    virtual ssize_t mc_send(const void *buf, size_t len, int flags,
                            u_long send_labels, 
                            resume_handler_t resume_handler, void *arg);
    virtual int mc_writev(const struct iovec *vec, int count,
                          u_long send_labels, 
                          resume_handler_t resume_handler, void *arg);
    virtual int mc_shutdown(int how);

    virtual irob_id_t mc_begin_irob(int numdeps, const irob_id_t *deps, 
                                    u_long send_labels, 
                                    resume_handler_t rh, void *rh_arg);
    virtual int mc_end_irob(irob_id_t id);
    virtual ssize_t mc_irob_send(irob_id_t id, 
                                 const void *buf, size_t len, int flags);
    virtual int mc_irob_writev(irob_id_t id, 
                               const struct iovec *vector, int count);
    virtual int irob_relabel(irob_id_t id, u_long new_labels);

    virtual int mc_get_failure_timeout(u_long label, struct timespec *ts);
    virtual int mc_set_failure_timeout(u_long label, const struct timespec *ts);
  private:
    mc_socket_t sock;
};

//#define FAKE_SOCKET_MAGIC_LABELS 0xDECAFBAD

//void set_socket_labels(int osfd, u_long labels);

#endif /* include guard */
