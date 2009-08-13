#include "cmm_thread.h"
#include "debug.h"
#include <stdexcept>
#include <pthread.h>
#include <assert.h>

struct thread_arg {
    CMMThread *thread;
    pthread_mutex_t starter_mutex;
    pthread_cond_t starter_cv;

    thread_arg(CMMThread *thread_) : thread(thread_) {
	pthread_mutex_init(&starter_mutex, NULL);
	pthread_cond_init(&starter_cv, NULL);
    }
};

void
ThreadCleanup(void * arg)
{
    struct thread_arg *th_arg = (struct thread_arg *)arg;
    assert(th_arg);

    CMMThread *thread = (CMMThread *)th_arg->thread;
    assert(thread);
    thread->running = false;
    thread->Finish();

    delete th_arg;
}

void *
ThreadFn(void * arg)
{
    pthread_cleanup_push(ThreadCleanup, arg);

    struct thread_arg *th_arg = (struct thread_arg *)arg;
    assert(th_arg);
    CMMThread *thread = th_arg->thread;
    assert(thread);
    try {
	pthread_mutex_lock(&th_arg->starter_mutex);
        thread->running = true;
	pthread_cond_signal(&th_arg->starter_cv);
	pthread_mutex_unlock(&th_arg->starter_mutex);

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
    struct thread_arg *arg = new struct thread_arg(this);

    int rc = pthread_create(&tid, NULL, ThreadFn, arg);
    if (rc < 0) {
	return rc;
    }

    pthread_mutex_lock(&arg->starter_mutex);
    while (!running) {
	pthread_cond_wait(&arg->starter_cv, &arg->starter_mutex);
    }
    pthread_mutex_unlock(&arg->starter_mutex);

    return 0;
}

CMMThread::CMMThread()
    : tid(0), running(false)
{
}

void
CMMThread::stop()
{
    if (running) {
        if (pthread_cancel(tid) == 0) {
            pthread_join(tid, NULL);
        }
    }
}

void
CMMThread::join()
{
    if (running) {
        pthread_join(tid, NULL);
    }
}
