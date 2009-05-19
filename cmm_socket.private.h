#ifndef cmm_socket_private_h
#define cmm_socket_private_h

#include "cmm_socket.h"

struct csocket {
    int osfd;
    u_long cur_label;
    int connected;

    csocket(int family, int type, int protocol);
    ~csocket();
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

template <typename T>
struct MyHashCompare {
    size_t hash(T sock) const { return (size_t)sock; }
    bool equal(T s1, T s2) const { return s1==s2; }
};

typedef tbb::concurrent_hash_map<mc_socket_t, 
                                 CMMSocketPtr, 
                                 MyHashCompare<mc_socket_t> > CMMSockHash;

struct thunk {
    resume_handler_t fn;
    void *arg;
    u_long label; /* single label bit only; relax this in the future */
    mc_socket_t sock; /* the socket that this thunk was thunk'd on */

    thunk(resume_handler_t f, void *a, u_long lbl, mc_socket_t s) 
	: fn(f), arg(a), label(lbl), sock(s) {}
};

typedef tbb::concurrent_queue<struct thunk*> ThunkQueue;
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

typedef tbb::concurrent_hash_map<u_long, struct labeled_thunk_queue *,
                                  MyHashCompare<u_long> > ThunkHash;

typedef std::vector<std::pair<mc_socket_t, int> > mcSocketOsfdPairList;

class CMMSocketImpl : public CMMSocket {
  public:
    virtual int mc_connect(const struct sockaddr *serv_addr, socklen_t addrlen,
                           u_long initial_labels,
                           connection_event_cb_t label_down_cb,
                           connection_event_cb_t label_up_cb,
                           void *cb_arg);

    virtual ssize_t mc_send(const void *buf, size_t len, int flags,
                            u_long labels, resume_handler_t resume_handler, 
                            void *arg);
    virtual int mc_writev(const struct iovec *vec, int count,
                          u_long labels, resume_handler_t resume_handler, 
                          void *arg);    

  protected:
    static CMMSockHash cmm_sock_hash;
    static ThunkHash thunk_hash;

    /* check whether this label is available; if not, 
     * register the thunk or return an error if no thunk given */
    int preapprove(u_long labels, 
                   resume_handler_t resume_handler, void *arg);

    void enqueue_handler(u_long label, resume_handler_t fn, void *arg);

    virtual int non_blocking_connect(u_long initial_labels) = 0;

    /* make sure that the socket is ready to send data with up_label. */
    virtual int prepare(u_long up_label) = 0;

    virtual int setup(u_long up_label) = 0;
    virtual void teardown(u_long down_label) = 0;
    
    int set_all_sockopts(int osfd);

    /* append <mc_socket_t,osfd> pairs to this vector for each 
     * such mapping in this mc_socket. 
     * Returns 0 if all the mappings were appended, 
     *        -1 if there were no connected osfds. */
    virtual int get_real_fds(mcSocketOsfdPairList &osfd_list) = 0;
    virtual void poll_map_back(struct pollfd *origfd, 
			     const struct pollfd *realfd) = 0;

    CMMSocketImpl(int family, int type, int protocol);

    mc_socket_t sock;
    CSockHash sock_color_hash;
    CSockList csocks;

    int non_blocking; /* 1 if non blocking, 0 otherwise*/

    /* these are used for re-creating the socket */
    int sock_family; 
    int sock_type;
    int sock_protocol;
    SockOptHash sockopts;

    /* these are used for reconnecting the socket */
    struct sockaddr *addr; 
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
  private:
    static int make_real_fd_set(int nfds, fd_set *fds,
			     mcSocketOsfdPairList &osfd_list, 
			     int *maxosfd);
    static int make_mc_fd_set(fd_set *fds, 
                              const mcSocketOsfdPairList &osfd_list);

    int get_osfd(u_long label);
};


class CMMSocketSerial : public CMMSocketImpl {
  public:
    virtual int prepare(u_long up_label);
    virtual int setup(u_long up_label);
    virtual void teardown(u_long down_label);

    virtual int mc_getpeername(struct sockaddr *address, 
                               socklen_t *address_len);
    virtual int reset();
    virtual int mc_read(void *buf, size_t count);
    virtual int mc_getsockopt(int level, int optname, 
                              void *optval, socklen_t *optlen);
    virtual int mc_setsockopt(int level, int optname, 
                              const void *optval, socklen_t optlen);
    virtual int get_real_fds(mcSocketOsfdPairList &osfd_list);
    virtual void poll_map_back(struct pollfd *origfd, 
			     const struct pollfd *realfd);

 protected:
    friend class CMMSocketImpl;
    CMMSocketSerial(int family, int type, int protocol);
    virtual int non_blocking_connect(u_long initial_labels);

  private: 
    struct csocket *active_csock;
};

class CMMSocketParallel : public CMMSocketImpl {
  public:
    virtual int prepare(u_long up_label);
    virtual int setup(u_long up_label);
    virtual void teardown(u_long down_label);
};

class CMMSocketPassThrough : public CMMSocket {
  public:
    CMMSocketPassThrough(mc_socket_t sock);

    virtual int mc_getpeername(struct sockaddr *address, 
                               socklen_t *address_len);
    virtual int reset();
    virtual int mc_read(void *buf, size_t count);
    virtual int mc_getsockopt(int level, int optname, 
                              void *optval, socklen_t *optlen);
    virtual int mc_setsockopt(int level, int optname, 
                              const void *optval, socklen_t optlen);
    virtual int get_real_fds(mcSocketOsfdPairList &osfd_list);
    virtual void poll_map_back(struct pollfd *origfd, 
			     const struct pollfd *realfd);
};

#ifndef SO_CONNMGR_LABELS
#define SO_CONNMGR_LABELS 39
#endif
void set_socket_labels(int osfd, u_long labels)
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

#endif /* include guard */
