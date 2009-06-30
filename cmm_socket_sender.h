#ifndef cmm_socket_sender_h_incl
#define cmm_socket_sender_h_incl

#include <pthread.h>
#include <sys/types.h>
#include "cmm_socket.private.h"
#include "cmm_socket_control.h"
#include "intset.h"
#include <map>
#include "cmm_socket_scheduler.h"

class CMMSocketSender : public CMMSocketScheduler {
  public:
    explicit CMMSocketSender(CMMSocketImpl *sk_);
    ssize_t send(const void *buf, size_t len, int flags);
  protected:
    virtual void Run();
  private:
    
};

#endif
