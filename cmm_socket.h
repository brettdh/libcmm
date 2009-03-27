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

typedef tbb:concurrent_hash_map<mc_socket_t, 
                                struct cmm_sock*, 
                                MyHashCompare<mc_socket_t> > CMMSockHash;

typedef boost::shared_ptr<CMMSocket> CMMSocketPtr;

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
    
  protected:
    CMMSocket(int family, int type, int protocol);

    int setAllSockopts(int osfd);

    static CMMSockHash cmm_sock_hash;
    CMMSockHash::const_accessor read_ac;
    CMMSockHash::accessor write_ac;

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
};


class CMMSocketSerial : public CMMSocket {
  public:
    virtual int prepare(u_long up_label);
    virtual int setup(u_long up_label);
    virtual int teardown(u_long down_label);
    
  private: 
    CMMSocketSerial(int family, int type, int protocol);
    struct csocket *active_csock;
};

class CMMSocketParallel : public CMMSocket {
  public:
    CMMSocketParallel(int family, int type, int protocol);    

    virtual int prepare(u_long up_label);
    virtual int setup(u_long up_label);
    virtual int teardown(u_long down_label);
};

#endif
