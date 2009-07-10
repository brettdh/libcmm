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

class PendingReceiverIROB : public PendingIROB {
  public:
    PendingReceiverIROB(struct begin_irob_data data);
    
    /* have all the deps been satisfied? 
     * (only meaningful on the receiver side) */
    bool is_released(void);

    /* Read the next len bytes into buf. 
     * After this call, the first len bytes cannot be re-read. */
    ssize_t read_data(void *buf, size_t len);

    ssize_t numbytes();
  private:
    /* If this IROB is in the middle of being read, 
     * the reader might have stopped in the middle of a
     * chunk.  If so, this is the offset into the first chunk. */
    ssize_t offset;

    /* the number of bytes left in this IROB. */
    ssize_t num_bytes;
};

class PendingReceiverIROBLattice : public PendingIROBLattice {
  public:
    PendingReceiverIROBLattice(PendingIROBLattice::Predicate p);
    PendingReceiverIROB *next_ready_irob();
    void mark_ready(PendingReceiverIROB *pirob);
    void partially_read(PendingReceiverIROB *pirob);
    void release_dependents(PendingReceiverIROB *pirob);
  private:
    PendingIROBLattice::Predicate pred;
    tbb::concurrent_queue<PendingReceiverIROB*> ready_irobs;
    PendingReceiverIROB *partially_read_irob;
};

#endif
