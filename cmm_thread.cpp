#include "cmm_thread.h"
#include "debug.h"
#include <stdexcept>
#include <pthread.h>
#include <assert.h>

void
ThreadCleanup(void * arg)
{
    CMMThread *thread = (CMMThread *)arg;
    assert(thread);
    thread->running = false;
}

static void *
ThreadFn(void * arg)
{
    pthread_cleanup_push(ThreadCleanup, arg);

    CMMThread *thread = (CMMThread *)arg;
    assert(thread);
    try {
        thread->running = true;
        thread->Run();
    } catch(const std::exception& e) {
	dbgprintf("Thread %d exited: %s\n", pthread_self(), e.what());
    }

    pthread_cleanup_pop(1);
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
