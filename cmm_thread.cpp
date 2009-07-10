#include "cmm_thread.h"
#include "debug.h"
#include <stdexcept>
#include <pthread.h>

static void *
ThreadFn(void * arg)
{
    try {
        CMMThread *thread = (CMMThread *)arg;
        thread->Run();
    } catch(const std::exception& e) {
        dbgprintf("%s\n", e.what());
    }
    return NULL;
}

int
CMMThread::start()
{
    return pthread_create(&tid, NULL, ThreadFn, this);
}

CMMThread::~CMMThread()
{
    if (pthread_cancel(tid) == 0) {
        pthread_join(tid, NULL);
    }
}
