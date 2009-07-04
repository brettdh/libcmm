#ifndef cmm_internal_listener_h_incl
#define cmm_internal_listener_h_incl

#include "cmm_socket.private.h"

class ListenerThread : public CMMThread {
  public:
    ListenerThread(CMMSocketImpl *sk_);
    in_port_t port() const;
  protected:
    virtual void Run();
  private:
    CMMSocketImplPtr sk;
    int listener_sock;
    int listen_port;
};

#endif
