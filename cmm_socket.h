#ifndef cmm_socket_h
#define cmm_socket_h

#include <map>
#include <vector>

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

class CMMSocket {
  public:
    CMMSocket(int family, int type, int protocol);
    virtual ~CMMSocket();

    /* make sure that the socket is ready to send data with up_label. */
    virtual int prepare(u_long up_label) = 0;

    virtual int setup(u_long up_label) = 0;
    virtual int teardown(u_long down_label) = 0;
    
  private:
    mc_socket_t sock;
    CSockHash sock_color_hash;
    CSockList csocks;

    int non_blocking; /* 1 if non blocking, 0 otherwise*/
    struct csocket *active_csock; /* only non-NULL if this socket is serial. */

    int sock_family; /* these are used for re-creating the socket */
    int sock_type;
    int sock_protocol;
    SockOptHash sockopts;

    struct sockaddr *addr; /* these are used for reconnecting the socket */
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
    CMMSocketSerial(int family, int type, int protocol);

    virtual int prepare(u_long up_label);
    virtual int setup(u_long up_label);
    virtual int teardown(u_long down_label);
};

class CMMSocketParallel : public CMMSocket {
  public:
    CMMSocketParallel(int family, int type, int protocol);    

    virtual int prepare(u_long up_label);
    virtual int setup(u_long up_label);
    virtual int teardown(u_long down_label);
};

#endif
