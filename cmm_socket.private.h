#ifndef cmm_socket_private_h
#define cmm_socket_private_h

#include "cmm_socket.h"

class CMMSocketSerial : public CMMSocket {
  public:
    virtual int prepare(u_long up_label);
    virtual int setup(u_long up_label);
    virtual int teardown(u_long down_label);
    int mc_poll(struct pollfd fds[], nfds_t nfds, int timeout);
    int mc_getpeername(int socket, struct sockaddr *address, 
		       socklen_t *address_len);
    
  protected:
    virtual 
    int makeRealFdSet(int nfds, fd_set *fds,
		      std::vector<std::pair<mc_socket_t,int> > &osfd_list, 
		      int *maxosfd);

  private: 
    CMMSocketSerial(int family, int type, int protocol);
    struct csocket *active_csock;
};

class CMMSocketParallel : public CMMSocket {
  public:
    virtual int prepare(u_long up_label);
    virtual int setup(u_long up_label);
    virtual int teardown(u_long down_label);
};

class CMMSocketPassThrough : public CMMSocket {
  public:
    CMMSocketPassThrough(mc_socket_t sock);
};

#endif
