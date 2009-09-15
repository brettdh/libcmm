#ifndef cmm_thread_h_incl
#define cmm_thread_h_incl

#include <pthread.h>
#include <set>

extern pthread_key_t thread_name_key;
#define MAX_NAME_LEN 20
char * get_thread_name();
void set_thread_name(const char *name);

class CMMThread {
  public:
    int start();
    void stop();
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
    pthread_mutex_t starter_mutex;
    pthread_cond_t starter_cv;

    friend void *ThreadFn(void *);
    friend void ThreadCleanup(void *);

  private:
    static pthread_mutex_t joinable_lock;
    static std::set<pthread_t> joinable_threads;
};

/* throw from Run() function to terminate thread */
class CMMThreadFinish {};

#endif
