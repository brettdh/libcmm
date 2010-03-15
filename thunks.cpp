#include "thunks.h"
#include "common.h"
#include "debug.h"
#include "pthread_util.h"
#include "cmm_socket.private.h"
#include <cassert>
#include "tbb/concurrent_hash_map.h"
#include "tbb/concurrent_queue.h"
#include <string.h>
#include <vector>
using std::vector;

struct thunk {
    resume_handler_t fn;
    void *arg;
    u_long send_labels; /* single label bit only; relax this in the future */
    mc_socket_t sock; /* the socket that this thunk was thunk'd on */

    thunk(resume_handler_t f, void *a, u_long slbl, mc_socket_t s) 
	: fn(f), arg(a), send_labels(slbl), sock(s) {}
    bool operator<(const struct thunk& other) {
        return (sock < other.sock ||
                send_labels < other.send_labels ||
                fn < other.fn ||
                arg < other.arg);
    }
};

template <typename T>
struct PtrLess {
    bool operator()(T *first, T *second) {
        return *first < *second;
    }
};

typedef tbb::concurrent_queue<struct thunk*> ThunkQueue;
struct labeled_thunk_queue {
    mc_socket_t sock;
    u_long send_labels; /* single label bit only; relax this in the future */
    typedef std::multiset<struct thunk *, PtrLess<struct thunk> > thunk_map_t;
    typedef std::pair<thunk_map_t::iterator, thunk_map_t::iterator> range_t;
    thunk_map_t thunk_map;
    ThunkQueue thunk_queue;

    int cancel_thunks(resume_handler_t fn, void *arg,
                      void (*deleter)(void*)) {
        int thunks_cancelled = 0;
        struct thunk dummy(fn, arg, send_labels, sock);
        range_t range = thunk_map.equal_range(&dummy);
        for (thunk_map_t::iterator it = range.first; 
             it != range.second; ++it) {
            struct thunk *victim = *it;
            victim->fn = NULL;
            if (deleter) {
                deleter(victim->arg);
            }
            victim->arg = NULL;
            victim->send_labels = 0;
            victim->sock = -1;
            thunks_cancelled++;
        }
        thunk_map.erase(range.first, range.second);
        return thunks_cancelled;
    }

    labeled_thunk_queue(mc_socket_t sk, u_long s)
        : sock(sk), send_labels(s) {}
    ~labeled_thunk_queue() {
	while (!thunk_queue.empty()) {
	    struct thunk *th = NULL;
	    thunk_queue.pop(th);
	    assert(th);
	    /* XXX: this leaks any outstanding thunk args.
	     * maybe we can assume that a thunk queue being destroyed means
	     * that the program is exiting. */
	    delete th;
	}
    }
};

struct tq_key {
    mc_socket_t sock;
    u_long send_label;

    tq_key(mc_socket_t sk, u_long s) 
        : sock(sk), send_label(s) {}

    bool operator<(const struct tq_key& other) const {
        return (sock < other.sock) || (send_label < other.send_label);
    }
};

typedef LockingMap<struct tq_key, struct labeled_thunk_queue *> ThunkHash;
static ThunkHash thunk_hash;

void enqueue_handler(mc_socket_t sock, u_long send_labels,
                     resume_handler_t fn, void *arg)
{
    ThunkHash::accessor hash_ac;
    if (!thunk_hash.find(hash_ac, tq_key(sock, send_labels))) {
	struct labeled_thunk_queue *new_tq;
        new_tq = new struct labeled_thunk_queue(sock, send_labels);
	thunk_hash.insert(hash_ac, tq_key(sock, send_labels));
	hash_ac->second = new_tq;
    }

    struct thunk * new_thunk = new struct thunk(fn, arg, send_labels, 
                                                sock);

    hash_ac->second->thunk_queue.push(new_thunk);


    dbgprintf("Registered thunk %p, arg %p on mc_sock %d send labels %lu\n",
              fn, arg, sock, send_labels);
    //print_thunks();
}

void print_thunks(void)
{
#ifdef CMM_DEBUG
    for (ThunkHash::iterator tq_iter = thunk_hash.begin();
	 tq_iter != thunk_hash.end(); tq_iter++) {
	struct labeled_thunk_queue *tq = tq_iter->second;
	dbgprintf("Send labels %lu, %d thunks\n",
		tq->send_labels, (int)tq->thunk_queue.size());
	for (ThunkQueue::iterator th_iter = tq->thunk_queue.begin();
	     th_iter != tq->thunk_queue.end(); th_iter++) {
	    struct thunk *th = *th_iter;
	    dbgprintf("    Thunk %p, arg %p, send labels %lu\n",
		    th->fn, th->arg, th->send_labels);
	}
    }
#endif
}

static void fire_one_thunk(struct thunk *th)
{
    // TODO: pool of thunk worker threads?
    // TODO: or maybe pass many thunks to one new thread?
    assert(th);
    assert(th->fn);
    
    th->fn(th->arg);

    /* clean up finished/cancelled thunks */
    delete th;
}

static void *ThunkThreadFn(void *arg)
{
    struct labeled_thunk_queue *tq = (struct labeled_thunk_queue*)arg;
    assert(tq);

    ThunkQueue q_copy;
    while (!tq->thunk_queue.empty()) {
        struct thunk *th = NULL;
        tq->thunk_queue.pop(th);
        assert(th);
        q_copy.push(th);
    }

    while (!q_copy.empty()) {
        struct thunk *th = NULL;
        q_copy.pop(th);
        assert(th);
        /* No worries if the app cancels the thunk after 
         * it is fired; this can happen even if we 
         * mutex'd the thunk here */
        if (th->fn) {
            fire_one_thunk(th);
            /* application was required to free() or save th->arg */
        } else {
            delete th;
        }

	if (!CMMSocketImpl::net_available(tq->sock, 
                                          tq->send_labels)) {
            /* Prevent spurious thunk-firing if the network
             * goes away while a thunk is being handled. 
             * Also avoids infinite loop resulting from
             * a thunk that itself registers a thunk. */
            break;
        }
    }
    return NULL;
}

static void fire_one_thunk_queue(struct labeled_thunk_queue *tq)
{
    pthread_t tid;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    (void)pthread_create(&tid, &attr, ThunkThreadFn, tq);
}

void fire_thunks(void)
{
    /* Handlers are fired:
     *  -for the same label in the order they were enqueued, and
     *  -for different labels in arbitrary order. */
    for (ThunkHash::iterator tq_iter = thunk_hash.begin(true, true);
	 tq_iter != thunk_hash.end(); tq_iter++) {
	struct labeled_thunk_queue *tq = tq_iter->second;
	if (CMMSocketImpl::net_available(tq->sock, 
                                         tq->send_labels)) {
            if (!tq->thunk_queue.empty()) {
                fire_one_thunk_queue(tq);
            }
	}
    }
}

void cancel_all_thunks(mc_socket_t sock)
{
    /* XXX: potentially expensive, but probably rare. */
    vector<struct labeled_thunk_queue*> victims;

    for (ThunkHash::iterator tq_iter = thunk_hash.begin(true, true);
	 tq_iter != thunk_hash.end(); tq_iter++) {
	struct labeled_thunk_queue *tq = tq_iter->second;
        if (tq->sock == sock) {
            victims.push_back(tq);
	}
    }
    
    for (size_t i = 0; i < victims.size(); ++i) {
        struct labeled_thunk_queue *tq = victims[i];
        thunk_hash.erase(tq_key(sock, tq->send_labels));
        delete tq;
    }
}

int cancel_thunk(mc_socket_t sock, u_long send_labels,
		 void (*handler)(void*), void *arg,
		 void (*deleter)(void*))
{
    ThunkHash::accessor hash_ac;
    if (!thunk_hash.find(hash_ac, tq_key(sock, send_labels))) {
	return 0;
    }
    struct labeled_thunk_queue *tq = hash_ac->second;
    
    return tq->cancel_thunks(handler, arg, deleter);
}
