#include "thunks.h"
#include "common.h"
#include <cassert>
#include "tbb/concurrent_hash_map.h"
#include "tbb/concurrent_queue.h"

struct thunk {
    resume_handler_t fn;
    void *arg;
    u_long label; /* single label bit only; relax this in the future */
    mc_socket_t sock; /* the socket that this thunk was thunk'd on */

    thunk(resume_handler_t f, void *a, u_long lbl, mc_socket_t s) 
	: fn(f), arg(a), label(lbl), sock(s) {}
};

typedef tbb::concurrent_queue<struct thunk*> ThunkQueue;
struct labeled_thunk_queue {
    u_long label; /* single label bit only; relax this in the future */
    ThunkQueue thunk_queue;

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

typedef tbb::concurrent_hash_map<u_long, struct labeled_thunk_queue *,
				 MyHashCompare<u_long> > ThunkHash;
static ThunkHash thunk_hash;

void enqueue_handler(mc_socket_t sock, 
		     u_long label, resume_handler_t fn, void *arg)
{
    ThunkHash::accessor hash_ac;
    if (!thunk_hash.find(hash_ac, label)) {
	struct labeled_thunk_queue *new_tq = new struct labeled_thunk_queue;
	new_tq->label = label;
	thunk_hash.insert(hash_ac, label);
	hash_ac->second = new_tq;
    }

    struct thunk * new_thunk = new struct thunk(fn, arg, label, sock);

    hash_ac->second->thunk_queue.push(new_thunk);


    fprintf(stderr, "Registered thunk %p, arg %p on mc_sock %d label %lu.\n", 
	    fn, arg, sock, label);
    //print_thunks();
}

void print_thunks(void)
{
    for (ThunkHash::const_iterator tq_iter = thunk_hash.begin();
	 tq_iter != thunk_hash.end(); tq_iter++) {
	struct labeled_thunk_queue *tq = tq_iter->second;
	fprintf(stderr, "Label %lu, %d thunks\n",
		tq->label, tq->thunk_queue.size());
	for (ThunkQueue::const_iterator th_iter = tq->thunk_queue.begin();
	     th_iter != tq->thunk_queue.end(); th_iter++) {
	    struct thunk *th = *th_iter;
	    fprintf(stderr, "    Thunk %p, arg %p, label %lu\n",
		    th->fn, th->arg, th->label);
	}
    }
}

void fire_thunks(u_long cur_labels)
{
    /* Handlers are fired:
     *  -for the same label in the order they were enqueued, and
     *  -for different labels in arbitrary order. */
    for (ThunkHash::iterator tq_iter = thunk_hash.begin();
	 tq_iter != thunk_hash.end(); tq_iter++) {
	struct labeled_thunk_queue *tq = tq_iter->second;
	if (tq->label & cur_labels) {
	    while (!tq->thunk_queue.empty()) {
		struct thunk *th = NULL;
		tq->thunk_queue.pop(th);
		assert(th);
		if (th->fn) {
		    th->fn(th->arg);
		    /* application was required to free() or save th->arg */
		}
		/* clean up finished/cancelled thunks */
		delete th;
	    }
	}
    }
}

int cancel_thunk(u_long label, 
		 void (*handler)(void*), void *arg,
		 void (*deleter)(void*))
{
    int thunks_cancelled = 0;
    ThunkHash::accessor hash_ac;
    if (!thunk_hash.find(hash_ac, label)) {
	return 0;
    }
    struct labeled_thunk_queue *tq = hash_ac->second;
    
    for (ThunkQueue::iterator it = tq->thunk_queue.begin();
	 it != tq->thunk_queue.end(); it++) {
	struct thunk *& victim = *it;
	if (victim->fn == handler && victim->arg == arg) {
	    if (deleter) {
		deleter(victim->arg);
	    }
	    victim->arg = NULL;
	    victim->fn = NULL;
	    victim->label = 0;
	    thunks_cancelled++;
	    /* this thunk will be cleaned up later */
	}
    }

    return thunks_cancelled;
}
