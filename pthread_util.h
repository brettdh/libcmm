#ifndef pthread_util_incl
#define pthread_util_incl

#include <pthread.h>

class PthreadScopedLock {
  public:
    explicit PthreadScopedLock(pthread_mutex_t *mutex_) : mutex(mutex_) {
        assert(mutex);
        pthread_mutex_lock(mutex);
    }
    ~PthreadScopedLock() {
        if (mutex) {
            pthread_mutex_unlock(mutex);
        }
    }
    void release() {
        pthread_mutex_unlock(mutex);
        mutex = NULL;
    }
  private:
    pthread_mutex_t *mutex;
};

class PthreadScopedRWLock {
  public:
    explicit PthreadScopedRWLock(pthread_rwlock_t *mutex_, bool writer) : mutex(mutex_) {
        assert(mutex);
        if (writer) {
            pthread_rwlock_wrlock(mutex);
        } else {
            pthread_rwlock_rdlock(mutex);
        }
    }
    ~PthreadScopedRWLock() {
        if (mutex) {
            pthread_rwlock_unlock(mutex);
        }
    }
    void release() {
        pthread_rwlock_unlock(mutex);
        mutex = NULL;
    }
  private:
    pthread_rwlock_t *mutex;

};

#endif
