#ifndef cmm_conn_bootstrapper_h_incl
#define cmm_conn_bootstrapper_h_incl

#include "cmm_socket.private.h"
#include "cmm_thread.h"

class ConnBootstrapper : public CMMThread {
  public:
    ConnBootstrapper(CMMSocketImpl *sk_, int bootstrap_sock_,
                     const struct sockaddr *addr, socklen_t addrlen);
    ~ConnBootstrapper();
    virtual void stop();

    bool done();
    bool succeeded();
    int status();
  protected:
    virtual void Run();
    virtual void Finish();
  private:
    CMMSocketImpl *sk;
    int bootstrap_sock;
    int status_;

    char *remote_addr;
    socklen_t addrlen;
};

#endif
