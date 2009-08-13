#ifndef cmm_thread_h_incl
#define cmm_thread_h_incl

#include <pthread.h>

class CMMThread {
  public:
    int start();
    void stop();
    void join();

    CMMThread();
    virtual ~CMMThread() {}
  protected:
    virtual void Run(void) = 0;

    /* This function will be called in this thread's context after 
     * the Run method finishes, whether it returns or throws an 
     * exception. */
    virtual void Finish(void) {}

    pthread_t tid;
    bool running;

    friend void *ThreadFn(void *);
    friend void ThreadCleanup(void *);
};

/* throw from Run() function to terminate thread */
class CMMThreadFinish {};

#endif
