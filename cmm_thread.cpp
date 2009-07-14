#include "cmm_thread.h"
#include "debug.h"
#include <stdexcept>
#include <pthread.h>
#include <assert.h>

static void *
ThreadFn(void * arg)
{
    CMMThread *thread = (CMMThread *)arg;
    assert(thread);
    try {
        thread->running = true;
        thread->Run();
    } catch(const std::exception& e) {
	dbgprintf("Thread %d exited: %s\n", pthread_self(), e.what());
    }
    thread->running = false;
    return NULL;
}

int
CMMThread::start()
{
    return pthread_create(&tid, NULL, ThreadFn, this);
}

CMMThread::CMMThread()
    : running(false), tid(0)
{
}

CMMThread::~CMMThread()
{
    if (running) {
        if (pthread_cancel(tid) == 0) {
            pthread_join(tid, NULL);
        }
    }
}
