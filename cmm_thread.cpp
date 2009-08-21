#include "cmm_thread.h"
#include "debug.h"
#include <stdexcept>
#include <pthread.h>
#include <assert.h>
#include "pthread_util.h"

void
ThreadCleanup(void * arg)
{
    CMMThread *thread = (CMMThread *)arg;
    assert(thread);
    pthread_mutex_lock(&thread->starter_mutex);
    thread->running = false;
    pthread_mutex_unlock(&thread->starter_mutex);
    thread->Finish();
}

void *
ThreadFn(void * arg)
{
    pthread_cleanup_push(ThreadCleanup, arg);

    CMMThread *thread = (CMMThread*)arg;
    assert(thread);
    try {
	pthread_mutex_lock(&thread->starter_mutex);
        thread->running = true;
	pthread_cond_signal(&thread->starter_cv);
	pthread_mutex_unlock(&thread->starter_mutex);

        thread->Run();
	dbgprintf("Thread %08x exited normally.\n", pthread_self());
    } catch(const std::exception& e) {
	dbgprintf("Thread %08x exited: %s\n", pthread_self(), e.what());
    } catch(const CMMThreadFinish& e) {
	dbgprintf("Thread %08x was cancelled via exception.\n", pthread_self());
    }

    pthread_cleanup_pop(1);
    return NULL;
}

int
CMMThread::start()
{
    int rc = pthread_create(&tid, NULL, ThreadFn, this);
    if (rc != 0) {
        dbgprintf("Failed to create thread! rc=%d\n", rc);
        return rc;
    }

    pthread_mutex_lock(&starter_mutex);
    while (!running) {
	pthread_cond_wait(&starter_cv, &starter_mutex);
    }
    pthread_mutex_unlock(&starter_mutex);

    return 0;
}

CMMThread::CMMThread()
    : tid(0), running(false)
{
    pthread_mutex_init(&starter_mutex, NULL);
    pthread_cond_init(&starter_cv, NULL);
}

void
CMMThread::stop()
{
    PthreadScopedLock lock(&starter_mutex);
    if (running) {
        lock.release();
        if (pthread_cancel(tid) == 0) {
            pthread_join(tid, NULL);
        }
    }
}

void
CMMThread::join()
{
    PthreadScopedLock lock(&starter_mutex);
    if (running) {
        lock.release();
        pthread_join(tid, NULL);
    }
}
