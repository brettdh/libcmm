#include "thunks.h"
#include "common.h"
#include "cmm_socket.private.h"
#include <cassert>
#include "tbb/concurrent_hash_map.h"
#include "tbb/concurrent_queue.h"

struct thunk {
    resume_handler_t fn;
    void *arg;
    u_long send_labels; /* single label bit only; relax this in the future */
    u_long recv_labels;
    mc_socket_t sock; /* the socket that this thunk was thunk'd on */

    thunk(resume_handler_t f, void *a, u_long slbl, u_long rlbl, mc_socket_t s) 
	: fn(f), arg(a), send_labels(slbl), recv_labels(rlbl), sock(s) {}
};

typedef tbb::concurrent_queue<struct thunk*> ThunkQueue;
struct labeled_thunk_queue {
    mc_socket_t sock;
    u_long send_labels; /* single label bit only; relax this in the future */
    u_long recv_labels;
    ThunkQueue thunk_queue;


    labeled_thunk_queue(mc_socket_t sk, u_long s, u_long r)
        : sock(sk), send_labels(s), recv_labels(r) {}
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
    u_long recv_label;

    tq_key(mc_socket_t sk, u_long s, u_long r) 
        : sock(sk), send_label(s), recv_label(r) {}
};

struct TQHashCompare {
    size_t hash(struct tq_key key) {
        /* collision-prone, but hey, the key-space is small */
        return key.sock ^ key.send_label ^ key.recv_label;
    }
    bool equal(struct tq_key this_one, struct tq_key that_one) {
        return memcmp(&this_one, &that_one, sizeof(struct tq_key)) == 0;
    }
};

typedef tbb::concurrent_hash_map<struct tq_key, struct labeled_thunk_queue *,
				 TQHashCompare> ThunkHash;
static ThunkHash thunk_hash;

void enqueue_handler(mc_socket_t sock, u_long send_labels, u_long recv_labels, 
                     resume_handler_t fn, void *arg)
{
    ThunkHash::accessor hash_ac;
    if (!thunk_hash.find(hash_ac, tq_key(sock, send_labels, recv_labels))) {
	struct labeled_thunk_queue *new_tq;
        new_tq = new struct labeled_thunk_queue(sock, send_labels, recv_labels);
	thunk_hash.insert(hash_ac, tq_key(sock, send_labels, recv_labels));
	hash_ac->second = new_tq;
    }

    struct thunk * new_thunk = new struct thunk(fn, arg, send_labels, recv_labels, 
                                                sock);

    hash_ac->second->thunk_queue.push(new_thunk);


    fprintf(stderr, "Registered thunk %p, arg %p on mc_sock %d send labels %lu, "
            "recv_labels %lu\n", 
	    fn, arg, sock, send_labels, recv_labels);
    //print_thunks();
}

void print_thunks(void)
{
    for (ThunkHash::const_iterator tq_iter = thunk_hash.begin();
	 tq_iter != thunk_hash.end(); tq_iter++) {
	struct labeled_thunk_queue *tq = tq_iter->second;
	fprintf(stderr, "Send labels %lu, Recv labels %lu, %d thunks\n",
		tq->send_labels, tq->recv_labels, tq->thunk_queue.size());
	for (ThunkQueue::const_iterator th_iter = tq->thunk_queue.begin();
	     th_iter != tq->thunk_queue.end(); th_iter++) {
	    struct thunk *th = *th_iter;
	    fprintf(stderr, "    Thunk %p, arg %p, send labels %lu, "
                    "recv labels %lu\n",
		    th->fn, th->arg, th->send_labels, th->recv_labels);
	}
    }
}

void fire_thunks(void)
{
    /* Handlers are fired:
     *  -for the same label in the order they were enqueued, and
     *  -for different labels in arbitrary order. */
    for (ThunkHash::iterator tq_iter = thunk_hash.begin();
	 tq_iter != thunk_hash.end(); tq_iter++) {
	struct labeled_thunk_queue *tq = tq_iter->second;
	if (CMMSocketImpl::net_available(tq->sock, 
                                         tq->send_labels, tq->recv_labels)) {
	    while (!tq->thunk_queue.empty()) {
		struct thunk *th = NULL;
		tq->thunk_queue.pop(th);
		assert(th);
                resume_handler_t fn = th->fn;
                /* No worries if the app cancels the thunk after 
                 * it is fired; this can happen even if we 
                 * mutex'd the thunk here */
		if (fn) {
		    fn(th->arg);
		    /* application was required to free() or save th->arg */
		}
		/* clean up finished/cancelled thunks */
		delete th;
	    }
	}
    }
}

int cancel_thunk(mc_socket_t sock, u_long send_labels, u_long recv_labels, 
		 void (*handler)(void*), void *arg,
		 void (*deleter)(void*))
{
    int thunks_cancelled = 0;
    ThunkHash::accessor hash_ac;
    if (!thunk_hash.find(hash_ac, tq_key(sock, send_labels, recv_labels))) {
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
	    victim->send_labels = victim->recv_labels = 0;
	    thunks_cancelled++;
	    /* this thunk will be cleaned up later */
	}
    }

    return thunks_cancelled;
}
