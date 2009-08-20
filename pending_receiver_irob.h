#ifndef pending_receiver_irob_h_incl
#define pending_receiver_irob_h_incl

#include <queue>
#include <set>
#include "libcmm.h"
#include "cmm_socket_control.h"
//#include "cmm_socket.private.h"
#include "intset.h"
#include "pending_irob.h"
#include "tbb/concurrent_queue.h"
#include <queue>
#include <cassert>

class PendingReceiverIROB : public PendingIROB {
  public:
    // ints in the data structs should be in host byte order
    PendingReceiverIROB(struct begin_irob_data data,
			u_long send_labels, u_long recv_labels);
    PendingReceiverIROB(struct default_irob_data data,
			u_long send_labels, u_long recv_labels);
    virtual ~PendingReceiverIROB();
    bool add_chunk(struct irob_chunk_data&); /* host byte order */

    /* have all the deps been satisfied? 
     * (only meaningful on the receiver side) */
    bool is_released(void);

    /* Read the next len bytes into buf. 
     * After this call, the first len bytes cannot be re-read. */
    ssize_t read_data(void *buf, size_t len);

    ssize_t numbytes();

  private:
    friend class CMMSocketImpl;
    friend class CSocketReceiver;
    friend class PendingReceiverIROBLattice;

    /* If this IROB is in the middle of being read, 
     * the reader might have stopped in the middle of a
     * chunk.  If so, this is the offset into the first chunk. */
    ssize_t offset;

    struct irob_chunk_data partial_chunk;

    /* the number of bytes left in this IROB. */
    ssize_t num_bytes;
};

class PendingReceiverIROBLattice : public PendingIROBLattice {
  public:
    PendingReceiverIROBLattice();
    virtual ~PendingReceiverIROBLattice();

    ssize_t recv(void *buf, size_t len, int flags, u_long *recv_labels);

    /* First take: this won't ever return an incomplete IROB. 
     *  (we may want to loosen this restriction in the future) */
    /* Hard rule: this won't ever return an unreleased IROB. */
    PendingReceiverIROB *get_ready_irob();

    template <typename Predicate>
    void release_if_ready(PendingReceiverIROB *pirob, Predicate is_ready);

    template <typename Predicate>
    void release_dependents(PendingReceiverIROB *pirob, Predicate is_ready);

    void partially_read(PendingReceiverIROB *pirob);
    
    /* signify that the socket has been shut down for reading. */
    void shutdown(); 
  private:
    /* for now, pass IROBs to the app in the order in which they are released */
    std::queue<PendingReceiverIROB*> ready_irobs;
    void enqueue(PendingReceiverIROB *pirob);
    pthread_mutex_t ready_mutex;
    pthread_cond_t ready_cv;

    PendingReceiverIROB *partially_read_irob;
};

template <typename Predicate>
void
PendingReceiverIROBLattice::release_if_ready(PendingReceiverIROB *pirob,
                                             Predicate is_ready)
{
    if (is_ready(pirob)) {
        /* TODO: smarter strategy for ordering ready IROBs. */
        enqueue(pirob);
    }
}

template <typename Predicate>
void 
PendingReceiverIROBLattice::release_dependents(PendingReceiverIROB *pirob,
                                               Predicate is_ready)
{
    assert(pirob);
    for (irob_id_set::iterator it = pirob->dependents.begin();
         it != pirob->dependents.end(); it++) {
        
        PendingIROB *pi = this->find(*it);
        if (pi == NULL) {
            continue;
        }
        PendingReceiverIROB *dependent = dynamic_cast<PendingReceiverIROB*>(pi);
        assert(dependent);
        //dependent->dep_satisfied(pirob->id);
        release_if_ready(dependent, is_ready);
    }
}

class ReadyIROB {
  public:
    bool operator()(PendingIROB *pi) {
        assert(pi);
        PendingReceiverIROB *pirob = dynamic_cast<PendingReceiverIROB*>(pi);
        assert(pirob);
        return (pirob->is_complete() && pirob->is_released());
    }
};

#endif
