#ifndef cmm_thread_h_incl
#define cmm_thread_h_incl

#include <pthread.h>

extern pthread_key_t thread_name_key;
#define MAX_NAME_LEN 20
void set_thread_name(const char *name);

class CMMThread {
  public:
    int start();
    void stop();
    void join();

    CMMThread();
    virtual ~CMMThread() {}

    pthread_t tid;
  protected:
    virtual void Run(void) = 0;

    /* This function will be called in this thread's context after 
     * the Run method finishes, whether it returns or throws an 
     * exception. */
    virtual void Finish(void) {}

    bool running;
    pthread_mutex_t starter_mutex;
    pthread_cond_t starter_cv;

    friend void *ThreadFn(void *);
    friend void ThreadCleanup(void *);
};

/* throw from Run() function to terminate thread */
class CMMThreadFinish {};

#endif
