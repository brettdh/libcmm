#ifndef pthread_util_incl
#define pthread_util_incl

#include <pthread.h>
#include <boost/shared_ptr.hpp>
#include <map>
#include <vector>
#include <assert.h>
#include "debug.h"

#define PTHREAD_ASSERT_SUCCESS(rc)                      \
    do {                                                \
        if (rc != 0) {                                  \
            fprintf(stderr, "PTHREAD ERROR: %s\n",      \
                    strerror(rc));                      \
            assert(0);                                  \
        }                                               \
    } while (0)

template <typename T>
class ThreadsafePrimitive {
  public:
    explicit ThreadsafePrimitive(const T& v = T()) : val(v) {
        pthread_rwlock_init(&lock, NULL);
    }
    operator T() {
        PthreadScopedRWLock lk(&lock, false);
        return val;
    }
    ThreadsafePrimitive<T>& operator=(const T& v) {
        PthreadScopedRWLock lk(&lock, true);
        val = v;
        return *this;
    }
  private:
    T val;
    pthread_rwlock_t lock;
};

class PthreadScopedLock {
  public:
    PthreadScopedLock() : mutex(NULL) {}

    explicit PthreadScopedLock(pthread_mutex_t *mutex_) {
        mutex = NULL;
        acquire(mutex_);
    }

    void acquire(pthread_mutex_t *mutex_) {
        assert(mutex == NULL);
        assert(mutex_);
        mutex = mutex_;
        int rc = pthread_mutex_lock(mutex);
        PTHREAD_ASSERT_SUCCESS(rc);
    }
    
    ~PthreadScopedLock() {
        if (mutex) {
            int rc = pthread_mutex_unlock(mutex);
            PTHREAD_ASSERT_SUCCESS(rc);
        }
    }
    void release() {
        int rc = pthread_mutex_unlock(mutex);
        PTHREAD_ASSERT_SUCCESS(rc);

        mutex = NULL;
    }
  private:
    pthread_mutex_t *mutex;
};

class PthreadScopedRWLock {
  public:
    PthreadScopedRWLock() : mutex(NULL) {}

    explicit PthreadScopedRWLock(pthread_rwlock_t *mutex_, bool writer) {
        mutex = NULL;
        acquire(mutex_, writer);
    }

    void acquire(pthread_rwlock_t *mutex_, bool writer) {
        assert(mutex == NULL);
        assert(mutex_);
        mutex = mutex_;
        int rc = 0;
        if (writer) {
            rc = pthread_rwlock_wrlock(mutex);
        } else {
            rc = pthread_rwlock_rdlock(mutex);
        }
        PTHREAD_ASSERT_SUCCESS(rc);
    }

    ~PthreadScopedRWLock() {
        if (mutex) {
            int rc = pthread_rwlock_unlock(mutex);
            PTHREAD_ASSERT_SUCCESS(rc);
        }
    }
    void release() {
        int rc = pthread_rwlock_unlock(mutex);
        PTHREAD_ASSERT_SUCCESS(rc);
        
        mutex = NULL;
    }
  private:
    pthread_rwlock_t *mutex;

};

/* LockWrappedMap
 *
 * Simple wrapper around an STL map that makes
 * the basic operations (insert, find, erase)
 * thread-safe.  Useful if you want any per-object
 * locks to live inside the contained objects.
 */
template <typename KeyType, typename ValueType,
          typename ordering = std::less<KeyType> >
class LockWrappedMap {
    typedef std::map<KeyType, ValueType, ordering> MapType;

  public:
    LockWrappedMap() {
        pthread_mutex_init(&membership_lock, NULL);
    }

    ~LockWrappedMap() {
        pthread_mutex_destroy(&membership_lock);
    }

    typedef typename MapType::iterator iterator;

    bool insert(const KeyType& key, const ValueType& val) {
        PthreadScopedLock lock(&membership_lock);
        std::pair<iterator, bool> ret = 
            the_map.insert(std::make_pair(key, val));
        return ret.second;
    }

    bool find(const KeyType &key, ValueType& val) {
        PthreadScopedLock lock(&membership_lock);
        typename MapType::iterator pos = the_map.find(key);
        if (pos != the_map.end()) {
            val = pos->second;
            return true;
        } else {
            return false;
        }
    }

    bool erase(const KeyType &key) {
        PthreadScopedLock lock(&membership_lock);
        return (the_map.erase(key) == 1);
    }

    /* Not thread-safe, obviously. */
    iterator begin() {
        return the_map.begin();
    }

    /* Not thread-safe, obviously. */
    iterator end() {
        return the_map.end();
    }

  private:
    MapType the_map;
    pthread_mutex_t membership_lock;
};


/* LockingMap<KeyType, ValueType>
 *
 * Provides a TBB-like interface for a thread-safe
 *  STL-style std::map<KeyType, ValueType>.
 * Accessors are used with insert, find, and erase
 *  functions to provide fine-grained locking.
 * Thread-safe iteration can be achieved by
 *  passing appropriate arguments to 
 *  begin(bool locked, bool writer).
 */
template <typename KeyType, typename ValueType,
          typename ordering = std::less<KeyType> >
class LockingMap {
    struct node;
    typedef boost::shared_ptr<struct node> NodePtr;
    typedef std::map<KeyType, NodePtr, ordering> MapType;
    typedef std::pair<KeyType, ValueType> pair_type;

    struct node {
        pair_type val;
        pthread_rwlock_t lock;

        node(const KeyType& key) : val(key, ValueType()) {
            pthread_rwlock_init(&lock, NULL);
        }
    };

    class accessor_base {
      public:
        accessor_base() {}
        pair_type* operator->() {
            assert(my_node);
            return &my_node->val;
        }
        pair_type& operator*() {
            return *(operator->());
        }

        ~accessor_base() {
            release();
        }
        void release() {
            if (my_node) {
                //dbgprintf("Releasing lock %p\n", &my_node->lock);
                int rc = pthread_rwlock_unlock(&my_node->lock);
                PTHREAD_ASSERT_SUCCESS(rc);

                my_node.reset();
            }
        }
      protected:
        friend class LockingMap;
        NodePtr my_node;
      private:
        accessor_base(const accessor_base&);
        void operator=(const accessor_base&);
    };

  public:
    class const_accessor : public accessor_base {};
    class accessor : public accessor_base {};

    LockingMap();
    bool insert(accessor& ac, const KeyType& key);
    bool find(const_accessor& ac, const KeyType& key);
    bool find(accessor& ac, const KeyType& key);
    bool erase(const KeyType& key);
    bool erase(accessor& ac);

    class iterator {
        friend class LockingMap;
        typedef boost::shared_ptr<PthreadScopedRWLock> LockPtr;
        
        bool valid;
        bool locked;
        NodePtr item;
        typename MapType::iterator my_iter;
        LockingMap* my_map;
        LockPtr my_lock;
        std::vector<LockPtr> member_locks;

      public:
        pair_type* operator->() {
            assert(valid);
            return &item->val;
        }
        pair_type& operator*() {
            return *(operator->());
        }

        bool operator==(const iterator& other) {
            return my_iter == other.my_iter;
        }
        bool operator!=(const iterator& other) {
            return !operator==(other);
        }

        iterator& operator++() {
            ++my_iter;
            update();
            return *this;
        }
        pair_type* operator++(int) {
            pair_type *result = operator->();
            operator++();
            return result;
        }
        
        iterator() : valid(false), my_map(NULL) {}
        ~iterator() {
            if (locked) {
                while (!member_locks.empty()) {
                    // just to be 100% sure about the destruction order
                    member_locks.pop_back();
                }
            }
        }
                
      private:
        iterator(LockingMap *my_map_, bool locked_ = false, bool writer = false)
            : valid(false), locked(locked_),
              my_map(my_map_) {
            if (locked) {
                // holds readlock until the last copy of
                //  this iterator is destroyed
                LockPtr lock(new PthreadScopedRWLock(&my_map->membership_lock, 
                                                     false));
                my_lock = lock;

                // also holds all of the node locks
                for (typename MapType::iterator it = my_map->the_map.begin(); 
                     it != my_map->the_map.end(); ++it) {
                    PthreadScopedRWLock *member_lock = NULL;
                    member_lock = new PthreadScopedRWLock(&it->second->lock, 
                                                          writer);
                    LockPtr lockp(member_lock);
                    member_locks.push_back(lockp);
                }
            }
        }

        void update();
    };

    // if calling with locked == true, make sure not to call 
    //  on this map again until the first iterator returned
    //  from here is destroyed, along with all copies.
    iterator begin(bool locked = false, bool writer = false) {
        iterator it(this, locked, writer);
        it.my_iter = the_map.begin();
        it.update();
        return it;
    }
    iterator end() {
        iterator it(this);
        it.my_iter = the_map.end();
        return it;
    }

  private:
    MapType the_map;
    pthread_rwlock_t membership_lock;
};

template <typename KeyType, typename ValueType, typename ordering>
void LockingMap<KeyType,ValueType,ordering>::iterator::update()
{
    if (my_iter != my_map->the_map.end()) {
        item = my_iter->second;
        assert(my_iter->second);
        valid = true;
    }
}

template <typename KeyType, typename ValueType, typename ordering>
LockingMap<KeyType,ValueType,ordering>::LockingMap()
{
    pthread_rwlock_init(&membership_lock, NULL);
}

template <typename KeyType, typename ValueType, typename ordering>
bool LockingMap<KeyType,ValueType,ordering>::insert(accessor& ac, const KeyType& key)
{
    NodePtr target;
    {
        PthreadScopedRWLock lock(&membership_lock, true);
        if (the_map.find(key) != the_map.end()) {
            return false;
        }
        the_map[key] = NodePtr(new struct node(key));
        target = the_map[key];
    }

    //dbgprintf("Grabbing writelock %p\n", &target->lock);
    int rc = pthread_rwlock_wrlock(&target->lock);
    PTHREAD_ASSERT_SUCCESS(rc);
    ac.my_node = target;

    return true;
}

template <typename KeyType, typename ValueType, typename ordering>
bool LockingMap<KeyType,ValueType,ordering>::find(const_accessor& ac, const KeyType& key)
{
    NodePtr target;
    {
        PthreadScopedRWLock lock(&membership_lock, false);
        if (the_map.find(key) == the_map.end()) {
            return false;
        }

        target = the_map[key];
    }
    //dbgprintf("Grabbing readlock %p\n", &target->lock);
    int rc = pthread_rwlock_rdlock(&target->lock);
    PTHREAD_ASSERT_SUCCESS(rc);

    ac.my_node = target;

    return true;
}

template <typename KeyType, typename ValueType, typename ordering>
bool LockingMap<KeyType,ValueType,ordering>::find(accessor& ac, const KeyType& key)
{
    NodePtr target;
    {
        PthreadScopedRWLock lock(&membership_lock, false);
        if (the_map.find(key) == the_map.end()) {
            return false;
        }
        
        target = the_map[key];
    }
    //dbgprintf("Grabbing writelock %p\n", &target->lock);
    int rc = pthread_rwlock_wrlock(&target->lock);
    PTHREAD_ASSERT_SUCCESS(rc);
    ac.my_node = target;

    return true;
}

template <typename KeyType, typename ValueType, typename ordering>
bool LockingMap<KeyType,ValueType,ordering>::erase(const KeyType& key)
{
    {
        PthreadScopedRWLock lock(&membership_lock, true);
        if (the_map.find(key) == the_map.end()) {
            return false;
        }

        the_map.erase(key);
        // shared_ptr will delete at the right time
    }

    return true;
}

template <typename KeyType, typename ValueType, typename ordering>
bool LockingMap<KeyType,ValueType,ordering>::erase(accessor& ac)
{
    return erase(ac->first);
}
#endif
