#ifndef pending_receiver_irob_h_incl
#define pending_receiver_irob_h_incl

#include <queue>
#include <set>
#include "libcmm.h"
#include "cmm_socket_control.h"
#include "cmm_socket.private.h"
#include "intset.h"
#include "pending_irob.h"
#include "tbb/concurrent_queue.h"
#include <queue>

class PendingReceiverIROB : public PendingIROB {
  public:
    PendingReceiverIROB(struct begin_irob_data data,
			u_long send_labels, u_long recv_labels);
    PendingReceiverIROB(struct default_irob_data data,
			u_long send_labels, u_long recv_labels);
    
    bool add_chunk(struct irob_chunk_data&); /* host byte order */

    /* have all the deps been satisfied? 
     * (only meaningful on the receiver side) */
    bool is_released(void);

    /* Read the next len bytes into buf. 
     * After this call, the first len bytes cannot be re-read. */
    ssize_t read_data(void *buf, size_t len);

    ssize_t numbytes();
  protected:
    friend class CMMSocketReceiver;
    friend class PendingReceiverIROBLattice;
  private:
    /* If this IROB is in the middle of being read, 
     * the reader might have stopped in the middle of a
     * chunk.  If so, this is the offset into the first chunk. */
    ssize_t offset;

    struct irob_chunk_data partial_chunk;

    /* the number of bytes left in this IROB. */
    ssize_t num_bytes;
};

typedef tbb::concurrent_hash_map
    <irob_id_t, PendingReceiverIROB *,
     IntegerHashCompare<irob_id_t> > PendingReceiverIROBHash;

class PendingReceiverIROBLattice : public PendingIROBLattice {
  public:
    PendingReceiverIROBLattice();

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
    PendingReceiverIROBHash pending_irobs;

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
        enqueue(pirob);
    }
}

template <typename Predicate>
void 
PendingReceiverIROBLattice::release_dependents(PendingReceiverIROB *pirob,
                                               Predicate is_ready)
{
    assert(pirob);
    for (PendingIROB::irob_id_set::iterator it = pirob->dependents.begin();
         it != pirob->dependents.end(); it++) {
        PendingReceiverIROBHash::accessor ac;
        if (!pending_irobs.find(ac, *it)) {
            continue;
        }
        PendingReceiverIROB *dependent = ac->second;
        assert(dependent);
        dependent->dep_satisfied(pirob->id);
        release_if_ready(dependent, is_ready);
    }
}

#endif
