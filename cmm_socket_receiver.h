#ifndef cmm_socket_receiver_h_incl
#define cmm_socket_receiver_h_incl

#include <pthread.h>
#include <sys/types.h>
#include "cmm_socket.private.h"
#include "cmm_socket_control.h"
#include "intset.h"
#include <map>

typedef tbb::concurrent_hash_map<irob_id_t, PendingIROB *,
                                 IntegerHashCompare<irob_id_t> > 
                                   PendingIROBHash;

class CMMSocketReceiver {
  public:
    ssize_t read(void *buf, size_t len);

    CMMSocketReceiver(CMMSocketImpl *sk_);
    void RunReceiver(void);
    void enqueue(struct CMMSocketMessageHdr hdr);
    
    /* 1) If pirob is anonymous, add deps on all pending IROBs.
     * 2) Otherwise, add deps on all pending anonymous IROBs.
     * 3) Remove already-satisfied deps. */
    void correct_deps(PendingIROB *pi);
  private:
    pthread_t tid;
    CMMSocketImpl *sk;

    IntSet committed_irobs;
    PendingIROBHash pending_irobs;

    void dispatch(struct CMMSocketControlHdr hdr);

    typedef void (CMMSocketReceiver::*dispatch_fn_t)(struct CMMSocketControlHdr);
    static std::map<short, CMMSocketReceiver::dispatch_fn_t> dispatcher;

    void do_begin_irob(struct CMMSocketControlHdr hdr);
    void do_end_irob(struct CMMSocketControlHdr hdr);
    void do_irob_chunk(struct CMMSocketControlHdr hdr);
    void do_new_interface(struct CMMSocketControlHdr hdr);
    void do_down_interface(struct CMMSocketControlHdr hdr);
    void do_ack(struct CMMSocketControlHdr hdr);
    void unrecognized_control_msg(struct CMMSocketControlHdr hdr);
};

#endif
