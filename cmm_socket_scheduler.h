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
#include "pending_irob.h"

/* CMMSocketScheduler
 *
 * A thread that reads from a concurrent_queue of messages
 * and acts on each one by dispatching member functions based
 * on msg.type() for each message.
 * MsgType classes should also define cleanup() to free any
 * dynamically allocated resources.  It will be called
 * on any messages remaining in the queue when the thread
 * terminates.
 */
template <typename MsgClass>
class CMMSocketScheduler : public CMMThread {
  public:
    virtual ~CMMSocketScheduler();
    
    void enqueue(MsgClass msg);

  protected:
    PendingIROBLattice pending_irobs;
    
    typedef tbb::concurrent_queue<MsgClass> ControlMsgQueue;
    ControlMsgQueue msg_queue;

    /* Default: read messages from msg_queue and dispatch them, forever. 
     * Any std::exception raised prints e.what() and terminates the thread. 
     * Subclasses should call handle() in their constructor to 
     * dispatch on different message types (see cmm_socket_control.h). */
    virtual void Run(void);

    void dispatch(MsgClass msg);

    virtual void unrecognized_control_msg(MsgClass msg);

  private:
    class Handler {
      public:
        virtual void operator()(MsgClass) = 0;
    };
    
    template <typename T> 
    class HandlerImpl : public Handler {
      public:
        typedef void (T::*Function)(MsgClass);
        HandlerImpl(T *obj_, Function fn_) : obj(obj_), fn(fn_) {}
        virtual void operator()(MsgClass msg) {
            (obj->*fn)(msg);
        }
      private:
        T *obj;
        Function fn;
    };

    std::map<short, Handler*> dispatcher;

  public:
    template <typename T>
    void handle(short type, T *obj, void (T::*fn)(MsgClass));
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
class CMMControlException : public std::runtime_error {
  public:
    CMMControlException(const std::string&, MsgClass);
    MsgClass msg;
};

#define CMM_INVALID_RC -10

struct AppThread {
    pthread_mutex_t mutex;
    pthread_cond_t cv;
    long rc;

    AppThread() : rc(CMM_INVALID_RC) {
        pthread_mutex_init(&mutex, NULL);
        pthread_cond_init(&cv, NULL);
    }
};

class Exception {
  public:
    template <typename T>
    static CMMControlException<T> make(const std::string& str, T obj) {
        return CMMControlException<T>(str, obj);
    }
};

#include "cmm_socket_scheduler.cpp"

#endif
