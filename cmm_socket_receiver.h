#ifndef cmm_socket_receiver_h_incl
#define cmm_socket_receiver_h_incl

#include <pthread.h>
#include <sys/types.h>
#include "cmm_socket.private.h"
#include "cmm_socket_control.h"
#include "intset.h"
#include <map>

class CMMSocketReceiver : public CMMSocketScheduler {
  public:
    explicit CMMSocketReceiver(CMMSocketImpl *sk_);
    ssize_t recv(void *buf, size_t len, int flags);
    
    /* 1) If pirob is anonymous, add deps on all pending IROBs.
     * 2) Otherwise, add deps on all pending anonymous IROBs.
     * 3) Remove already-satisfied deps. */
    void correct_deps(PendingIROB *pi);
  protected:
    virtual void Run();
  private:

    IntSet committed_irobs;

    void do_begin_irob(struct CMMSocketControlHdr hdr);
    void do_end_irob(struct CMMSocketControlHdr hdr);
    void do_irob_chunk(struct CMMSocketControlHdr hdr);
    void do_new_interface(struct CMMSocketControlHdr hdr);
    void do_down_interface(struct CMMSocketControlHdr hdr);
    void do_ack(struct CMMSocketControlHdr hdr);
};

#endif
