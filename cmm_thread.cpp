#include "cmm_thread.h"
#include "debug.h"
#include <stdexcept>
#include <pthread.h>
#include <assert.h>
#include "pthread_util.h"

pthread_key_t thread_name_key;
static pthread_once_t key_once = PTHREAD_ONCE_INIT;

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

CMMThread::~CMMThread()
{
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
    PthreadScopedLock lock(&starter_mutex);
    if (running) {
        lock.release();
        pthread_join(tid, NULL);
    }
}
