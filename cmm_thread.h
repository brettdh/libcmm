#ifndef cmm_thread_h_incl
#define cmm_thread_h_incl

#include <pthread.h>
#include <set>

class CMMThread {
  public:
    int start();
    //virtual void stop();
    void join();

    CMMThread();
    virtual ~CMMThread();

    static void join_all();

    pthread_t tid;
  protected:
    virtual void Run(void) = 0;

    /* This function will be called in this thread's context after 
     * the Run method finishes, whether it returns or throws an 
     * exception. */
    virtual void Finish(void) {}

    /* may only be called by this thread, from Run() or Finish(). */
    void detach();

    bool running;
    bool exiting;
    bool start_has_returned;
    pthread_mutex_t starter_mutex;
    pthread_cond_t starter_cv;
    pthread_cond_t start_return_cv;

    friend void *ThreadFn(void *);
    friend void ThreadCleanup(void *);

  private:
    static pthread_mutex_t joinable_lock; 

    // put this on the heap.  it will persist until the process exits,
    //  so as to avoid nasty last-second deallocation races.
    static std::set<pthread_t> *joinable_threads;

    static std::set<pthread_t> *joinable_threads_set();
};

/* throw from Run() function to terminate thread */
class CMMThreadFinish {};

#endif
