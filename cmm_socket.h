#ifndef cmm_socket_h
#define cmm_socket_h

#include <map>
#include <vector>

#include <boost/shared_ptr.hpp>

struct csocket;

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

typedef boost::shared_ptr<CMMSocket> CMMSocketPtr;
typedef tbb:concurrent_hash_map<mc_socket_t, 
                                CMMSocketPtr, 
                                MyHashCompare<mc_socket_t> > CMMSockHash;


class CMMSocket {
  public:
    static mc_socket_t create(int family, int type, int protocol);
    static CMMSocketPtr lookup(mc_socket_t sock);
    static void close(mc_socket_t sock);

    virtual ~CMMSocket();

    /* make sure that the socket is ready to send data with up_label. */
    virtual int prepare(u_long up_label) = 0;

    virtual int setup(u_long up_label) = 0;
    virtual int teardown(u_long down_label) = 0;
    
    virtual int reset() = 0;

    static int mc_select(mc_socket_t nfds, 
			 fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
			 struct timeval *timeout);
    static int mc_poll(struct pollfd fds[], nfds_t nfds, int timeout);

    virtual int mc_getpeername(struct sockaddr *address, 
			       socklen_t *address_len) = 0;
    virtual int mc_read(void *buf, size_t count) = 0;
    virtual int mc_getsockopt(int level, int optname, 
                              void *optval, socklen_t *optlen) = 0;
    virtual int mc_setsockopt(int level, int optname, 
                              const void *optval, socklen_t optlen) = 0;

    
  protected:
    CMMSocket(int family, int type, int protocol);

    int setAllSockopts(int osfd);
    typedef std::vector<std::pair<mc_socket_t, int> > mcSocketOsfdPairList;

    /* append <mc_socket_t,osfd> pairs to this vector for each 
     * such mapping in this mc_socket. 
     * Returns 0 if all the mappings were appended, 
     *        -1 if there were no connected osfds. */
    virtual int getRealFds(mcSocketOsfdPairList &osfd_list) = 0;
    virtual void pollMapBack(struct pollfd *origfd, 
			     const struct pollfd *realfd) = 0;

    static CMMSockHash cmm_sock_hash;

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
    static int makeRealFdSet(int nfds, fd_set *fds,
			     mcSocketOsfdPairList &osfd_list, 
			     int *maxosfd) = 0;
    static int makeMcFdSet(fd_set *fds, mcSocketOsfdPairList &osfd_list);

};

#endif
