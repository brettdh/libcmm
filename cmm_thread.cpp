#include "cmm_thread.h"
#include "debug.h"
#include <stdexcept>
#include <pthread.h>
#include <assert.h>
#include "pthread_util.h"
#include <algorithm>
#include <functional>
using std::for_each; using std::bind2nd;
using std::ptr_fun;

pthread_key_t thread_name_key;
static pthread_once_t key_once = PTHREAD_ONCE_INIT;

pthread_mutex_t CMMThread::joinable_lock = PTHREAD_MUTEX_INITIALIZER;
std::set<pthread_t> CMMThread::joinable_threads;


void
ThreadCleanup(void * arg)
{
    CMMThread *thread = (CMMThread *)arg;
    assert(thread);
    pthread_mutex_lock(&thread->starter_mutex);
    thread->running = false;
    thread->exiting = true;
    pthread_cond_signal(&thread->starter_cv);
    pthread_mutex_unlock(&thread->starter_mutex);

    {
        PthreadScopedLock lock(&CMMThread::joinable_lock);
        CMMThread::joinable_threads.erase(thread->tid);
    }

    thread->Finish();
}

static void delete_name_string(void *arg)
{
    char *name_str = (char*)arg;
    delete [] name_str;
}

static void make_key()
{
    (void)pthread_key_create(&thread_name_key, delete_name_string);
    pthread_setspecific(thread_name_key, NULL);
}

void set_thread_name(const char *name)
{
    (void) pthread_once(&key_once, make_key);

    assert(name);
    char *old_name = (char*)pthread_getspecific(thread_name_key);
    delete [] old_name;

    char *name_str = new char[MAX_NAME_LEN+1];
    memset(name_str, 0, MAX_NAME_LEN+1);
    strncpy(name_str, name, MAX_NAME_LEN);
    pthread_setspecific(thread_name_key, name_str);
}

char * get_thread_name()
{
    (void) pthread_once(&key_once, make_key);

    char * name_str = (char*)pthread_getspecific(thread_name_key);
    if (!name_str) {
        char *name = new char[12];
        sprintf(name, "%08lx", pthread_self());
        pthread_setspecific(thread_name_key, name);
        name_str = name;
    }

    return name_str;
}

void *
ThreadFn(void * arg)
{
    pthread_cleanup_push(ThreadCleanup, arg);

    (void)get_thread_name();

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
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 1024*1024);

    {
        PthreadScopedLock lock(&joinable_lock);
        int rc = pthread_create(&tid, &attr, ThreadFn, this);
        if (rc != 0) {
            dbgprintf("Failed to create thread! rc=%d\n", rc);
            return rc;
        }
        dbgprintf("Created thread %x\n", tid);
        joinable_threads.insert(tid);
    }

#if 0
    pthread_mutex_lock(&starter_mutex);
    while (!running && !exiting) {
	pthread_cond_wait(&starter_cv, &starter_mutex);
    }
    if (exiting) {
        dbgprintf("Thread %x started, but is exiting "
                  "as start() returns\n",
                  tid);
    } else {
        dbgprintf("Thread %x is running\n", tid);
    }
    pthread_mutex_unlock(&starter_mutex);
#endif
    dbgprintf("Thread %x started\n", tid);

    return 0;
}

CMMThread::CMMThread()
    : tid(0), running(false), exiting(false)
{
    pthread_mutex_init(&starter_mutex, NULL);
    pthread_cond_init(&starter_cv, NULL);
}

CMMThread::~CMMThread()
{
    dbgprintf("CMMThread %p (tid %x) is being destroyed\n", 
              this, tid);
    pthread_mutex_destroy(&starter_mutex);
    pthread_cond_destroy(&starter_cv);
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
    {
        PthreadScopedLock lock(&starter_mutex);
        if (running) {
            lock.release();
            pthread_join(tid, NULL);
        }
    }

    PthreadScopedLock j_lock(&joinable_lock);
    joinable_threads.erase(tid);
}

void
CMMThread::detach()
{
    assert(tid == pthread_self());
    {
        PthreadScopedLock j_lock(&joinable_lock);
        joinable_threads.erase(tid);
    }
    
    pthread_detach(tid);
}

void
CMMThread::join_all()
{
    std::set<pthread_t> joinable_threads_private;
    {
        PthreadScopedLock lock(&joinable_lock);
        joinable_threads_private = joinable_threads;
        joinable_threads.clear();
    }
    void **result = NULL;
    for (std::set<pthread_t>::iterator it = joinable_threads_private.begin();
         it != joinable_threads_private.end(); it++) {
        dbgprintf("pthread_join to thread %x\n", *it);
        pthread_join(*it, result);
    }
}
