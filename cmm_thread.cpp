#include "cmm_thread.h"
#include "debug.h"
#include <stdexcept>
#include <pthread.h>
#include <assert.h>
#include <string.h>
#include "pthread_util.h"
#include <algorithm>
#include <functional>
using std::for_each; using std::bind2nd;
using std::ptr_fun;

pthread_mutex_t CMMThread::joinable_lock = MY_PTHREAD_MUTEX_INITIALIZER;
std::set<pthread_t> *CMMThread::joinable_threads = NULL;

// must be holding joinable_lock.
std::set<pthread_t> *
CMMThread::joinable_threads_set()
{
    if (joinable_threads == NULL) {
        joinable_threads = new std::set<pthread_t>;
    }
    return joinable_threads;
}

void
ThreadCleanup(void * arg)
{
    CMMThread *thread = (CMMThread *)arg;
    ASSERT(thread);
    pthread_mutex_lock(&thread->starter_mutex);
    thread->running = false;
    thread->exiting = true;
    pthread_cond_signal(&thread->starter_cv);
    pthread_mutex_unlock(&thread->starter_mutex);

    pthread_t tid = thread->tid;
    thread->Finish();

    {
        PthreadScopedLock lock(&CMMThread::joinable_lock);
        CMMThread::joinable_threads_set()->erase(tid);
    }
}

void *
ThreadFn(void * arg)
{
    //pthread_cleanup_push(ThreadCleanup, arg);

    (void)get_thread_name();

    CMMThread *thread = (CMMThread*)arg;
    ASSERT(thread);
    try {
        pthread_mutex_lock(&thread->starter_mutex);
        thread->running = true;
        pthread_cond_signal(&thread->starter_cv);
        
        while (!thread->start_has_returned) {
            pthread_cond_wait(&thread->start_return_cv, &thread->starter_mutex);
        }
        pthread_mutex_unlock(&thread->starter_mutex);

        thread->Run();
        dbgprintf("Thread %08lx exited normally.\n", pthread_self());
    } catch(const std::exception& e) {
        dbgprintf("Thread %08lx exited: %s\n", pthread_self(), e.what());
    } catch(const CMMThreadFinish& e) {
        dbgprintf("Thread %08lx was cancelled via exception.\n", pthread_self());
    }

    ThreadCleanup(arg);

    //pthread_cleanup_pop(1);
    return NULL;
}

int
CMMThread::start()
{
    PthreadScopedLock lock(&starter_mutex);
    start_has_returned = false;

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 1024*1024);

    {
        PthreadScopedLock j_lock(&joinable_lock);
        int rc = pthread_create(&tid, &attr, ThreadFn, this);
        if (rc != 0) {
            dbgprintf("Failed to create thread! rc=%d\n", rc);
            return rc;
        }
        dbgprintf("Created thread %lx\n", tid);
        joinable_threads_set()->insert(tid);
    }

    while (!running && !exiting) {
        pthread_cond_wait(&starter_cv, &starter_mutex);
    }
    if (exiting) {
        dbgprintf("Thread %lx started, but is exiting "
                  "as start() returns\n",
                  tid);
    } else {
        dbgprintf("Thread %lx is running\n", tid);
    }

    dbgprintf("Thread %lx started\n", tid);

    start_has_returned = true;
    pthread_cond_signal(&start_return_cv);
    
    return 0;
}

CMMThread::CMMThread()
    : tid(0), running(false), exiting(false), start_has_returned(false)
{
    MY_PTHREAD_MUTEX_INIT(&starter_mutex);
    pthread_cond_init(&starter_cv, NULL);
    pthread_cond_init(&start_return_cv, NULL);
}

CMMThread::~CMMThread()
{
    dbgprintf("CMMThread %p (tid %lx) is being destroyed\n", 
              this, tid);
    pthread_mutex_destroy(&starter_mutex);
    pthread_cond_destroy(&starter_cv);
    pthread_cond_destroy(&start_return_cv);
}

// void
// CMMThread::stop()
// {
//     PthreadScopedLock lock(&starter_mutex);
//     if (running) {
//         lock.release();
//         if (pthread_cancel(tid) == 0) {
//             pthread_join(tid, NULL);
//         }
//     }
// }

void
CMMThread::join()
{
    {
        PthreadScopedLock lock(&starter_mutex);
        if (running) {
            lock.release();
            pthread_join(tid, NULL);
        }
    }

    PthreadScopedLock j_lock(&joinable_lock);
    joinable_threads_set()->erase(tid);
}

void
CMMThread::detach()
{
    ASSERT(tid == pthread_self());
    {
        PthreadScopedLock j_lock(&joinable_lock);
        joinable_threads_set()->erase(tid);
    }
    
    pthread_detach(tid);
}

void
CMMThread::join_all()
{
    PthreadScopedLock lock(&joinable_lock);

    while (!joinable_threads_set()->empty()) {
        std::vector<pthread_t> joinable_threads_private(joinable_threads_set()->begin(),
                                                        joinable_threads_set()->end());
        joinable_threads_set()->clear();

        pthread_mutex_unlock(&joinable_lock);
        
        void **result = NULL;
        for (std::vector<pthread_t>::iterator it = joinable_threads_private.begin();
             it != joinable_threads_private.end(); it++) {
            dbgprintf("pthread_join to thread %lx\n", *it);
            pthread_join(*it, result);
        }

        pthread_mutex_lock(&joinable_lock);
    }
}
