#ifndef cmm_socket_scheduler_h_incl
#define cmm_socket_scheduler_h_incl

#include "cmm_thread.h"
#include <sys/types.h>
#include "cmm_socket.private.h"
#include "cmm_socket_control.h"
#include "intset.h"
#include <map>
#include "tbb/concurrent_hash_map.h"
#include "tbb/concurrent_queue.h"

/* CMMSocketScheduler
 *
 * A thread that reads from a concurrent_queue of messages
 * and acts on each one by dispatching member functions based
 * on msg.type() for each message.
 */
template <typename MsgClass>
class CMMSocketScheduler<MsgClass> : public CMMThread {
  public:
    virtual ~CMMSocketScheduler() {}
    
    void enqueue(MsgClass msg);

    /* 1) If pirob is anonymous, add deps on all pending IROBs.
     * 2) Otherwise, add deps on all pending anonymous IROBs.
     * 3) Remove already-satisfied deps. */
    void correct_deps(PendingReceiverIROB *pi);

  protected:
    /* In a sender, this means IROBs that have been sent and ACK'd.
     * In a receiver, this means IROBs that have been received by the app. */
    IntSet past_irobs;

    typedef tbb::concurrent_queue<MsgClass> ControlMsgQueue;
    ControlMsgQueue msg_queue;

    /* Default: read messages from msg_queue and dispatch them, forever. 
     * Any std::exception raised prints e.what() and terminates the thread. 
     * Subclasses should call handle() in their constructor to 
     * dispatch on different message types (see cmm_socket_control.h). */
    virtual void Run(void);

    void dispatch(MsgClass msg);

    typedef void (CMMSocketScheduler::*dispatch_fn_t)(MsgClass);
    void handle(short type, dispatch_fn_t handler);
    virtual void unrecognized_control_msg(MsgClass msg);

  private:
    std::map<short, CMMSocketReceiver::dispatch_fn_t> dispatcher;
};

#include <stdexcept>

/* These shouldn't ever be thrown in normal operation, when there's 
 * a multi-socket on each end and the two are working in harmony. 
 * When something is amiss, though, this should help to discover
 * the problem. 
 *
 * Classes to be used with this exception type should implement
 * describe(), a method that returns a std::string with any relevant
 * debugging information for its object.
 */
template <typename MsgClass>
class CMMControlException<MsgClass> : public std::runtime_error {
  public:
    CMMControlException(const std::string&, MsgClass);
    MsgClass msg;
};

#include "cmm_socket_scheduler.cpp"

#endif
