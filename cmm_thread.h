#ifndef cmm_thread_h_incl
#define cmm_thread_h_incl

#include <pthread.h>

class CMMThread {
  public:
    void start();
    /* void stop(); TODO */
    virtual ~CMMThread();
  protected:
    pthread_t tid;

    virtual void Run(void) = 0;
};

#endif
