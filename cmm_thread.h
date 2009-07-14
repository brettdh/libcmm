#ifndef cmm_thread_h_incl
#define cmm_thread_h_incl

#include <pthread.h>

class CMMThread {
  public:
    int start();
    /* void stop(); TODO */
    CMMThread();
    virtual ~CMMThread();
    virtual void Run(void) = 0;
    bool running;
  protected:
    pthread_t tid;
};

#endif
