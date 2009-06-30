#ifndef cmm_socket_scheduler_h_incl
#define cmm_socket_scheduler_h_incl

#include <pthread.h>
#include <sys/types.h>
#include "cmm_socket.private.h"
#include "cmm_socket_control.h"
#include "intset.h"
#include <map>
#include "tbb/concurrent_hash_map.h"
#include "tbb/concurrent_queue.h"

typedef tbb::concurrent_hash_map<irob_id_t, PendingIROB *,
                                 IntegerHashCompare<irob_id_t> > 
                                   PendingIROBHash;

typedef tbb::concurrent_queue<struct CMMSocketControlHdr> ControlMsgQueue;

class CMMSocketScheduler {
  public:
    explicit CMMSocketScheduler(CMMSocketImpl *sk_);
    void start();
    void enqueue(struct CMMSocketMessageHdr hdr);
    virtual ~CMMSocketScheduler();

  protected:
    pthread_t tid;
    CMMSocketImplPtr sk;

    ControlMsgQueue msg_queue;

    PendingIROBHash pending_irobs;

    /* Default: read messages from msg_queue and dispatch them, forever. 
     * Any std::exception raised prints e.what() and terminates the thread. 
     * Subclasses should call handle() in their constructor to 
     * dispatch on different message types (see cmm_socket_control.h). */
    virtual void Run(void);

    void dispatch(struct CMMSocketControlHdr hdr);

    typedef void (CMMSocketScheduler::*dispatch_fn_t)(struct CMMSocketControlHdr);
    void handle(short hdr_type, dispatch_fn_t handler);
    virtual void unrecognized_control_msg(struct CMMSocketControlHdr hdr);

  private:
    std::map<short, CMMSocketReceiver::dispatch_fn_t> dispatcher;
};

#endif
