#ifndef pending_receiver_irob_h_incl
#define pending_receiver_irob_h_incl

#include <queue>
#include <set>
#include "libcmm.h"
#include "cmm_socket_control.h"
//#include "cmm_socket.private.h"
#include "intset.h"
#include "pending_irob.h"
#include "irob_scheduling.h"
#include <cassert>
#include "debug.h"

class PendingReceiverIROB : public PendingIROB {
  public:
    // ints in the data structs should be in host byte order
    PendingReceiverIROB(irob_id_t id, int numdeps, irob_id_t *deps, 
                        size_t datalen, char *data,
                        u_long send_labels);
    virtual ~PendingReceiverIROB();
    bool add_chunk(struct irob_chunk_data&); /* host byte order */

    // construct and return a vector representing all (if any)
    //  chunks that this IROB is missing.  This will be called
    //  in response to a resend request.
    std::vector<struct irob_chunk_data> get_missing_chunks();

    // return the seqno of the "next chunk" that would be received;
    //  that is, the largest seqno I've received, plus 1.
    int next_chunk_seqno();

    /* have all the deps been satisfied? 
     * (only meaningful on the receiver side) */
    bool is_ready(void);

    /* The receiver can receive the END_IROB message before all
     * of the chunks have been received.  Therefore, the 
     * END_IROB message contains the total number of chunks
     * in the IROB, and is_complete() will return true
     * iff all of the chunks AND the END_IROB message
     * have arrived. */
    bool is_complete(void);

    bool all_chunks_complete();

    //  From this, get_missing_chunks()
    //  will know which seqnos haven't been received.
    bool finish(ssize_t expected_bytes, int num_chunks);

    /* Read the next len bytes into buf. 
     * After this call, the first len bytes cannot be re-read. */
    ssize_t read_data(void *buf, size_t len);

    ssize_t numbytes();
    ssize_t recvdbytes();

    // Copy metadata from other and take ownership of its data chunks.
    // other should be a placeholder.
    virtual void subsume(PendingIROB *other);

  private:
    PendingReceiverIROB(irob_id_t id);

    void assert_valid();

    friend class CMMSocketImpl;
    friend class CSocketSender;
    friend class CSocketReceiver;
    friend class PendingReceiverIROBLattice;

    /* If this IROB is in the middle of being read, 
     * the reader might have stopped in the middle of a
     * chunk.  If so, this is the offset into the first chunk. */
    size_t offset;

    struct irob_chunk_data partial_chunk;

    /* the number of bytes left in this IROB. */
    ssize_t num_bytes;

    /* total number of bytes expected for this IROB.
     * this is -1 until the END_IROB message arrives. */
    ssize_t expected_bytes;

    // Bytes should arrive in this many chunks with this many seqnos.
    //  Also -1 until the End_IROB message arrives.
    int expected_chunks;

    /* number of bytes received (duh).  Once
     * recvd_chunks == num_chunks and the END_IROB is
     * received, this IROB is_complete(). 
     * This number is strictly increasing, whereas
     * num_bytes above decreases as bytes are copied out
     * by read_data(). */
    ssize_t recvd_bytes;

    int recvd_chunks;
};

class CMMSocketImpl;

class PendingReceiverIROBLattice : public PendingIROBLattice {
  public:
    PendingReceiverIROBLattice(CMMSocketImpl *sk);
    virtual ~PendingReceiverIROBLattice();

    ssize_t recv(void *buf, size_t len, int flags, u_long *recv_labels);

    /* First take: this won't ever return an incomplete IROB. 
     *  (we may want to loosen this restriction in the future) */
    /* Hard rule: this won't ever return a non-ready IROB. */
    // also, if the socket is in blocking mode and block_for_data == true,
    //  this will block if no IROBs are ready.
    PendingReceiverIROB *get_ready_irob(bool block_for_data);

    bool data_is_ready();

    // must call with sk->scheduling_state_lock held
    template <typename Predicate>
    void release_if_ready(PendingReceiverIROB *pirob, Predicate is_ready);

    void partially_read(PendingReceiverIROB *pirob);
    
    /* signify that the socket has been shut down for reading. */
    void shutdown();

    // returns a placeholder IROB of the correct subtype for this lattice
    virtual PendingIROB *make_placeholder(irob_id_t id);

  private:
    CMMSocketImpl *sk; // for scheduling state locks
    /* for now, pass IROBs to the app in the order in which they are released */
    //std::set<irob_id_t> ready_irobs;
    IROBPrioritySet ready_irobs;

    // must call with sk->scheduling_state_lock held
    void release(irob_id_t id, u_long send_labels);

    // must call with sk->scheduling_state_lock held
    template <typename Predicate>
    void release_dependents(PendingReceiverIROB *pirob, Predicate is_ready);

    PendingReceiverIROB *partially_read_irob;

    // get_ready_irob will return &empty_sentinel_irob if the
    //  socket is non-blocking and there are no more IROBs
    //  ready.
    static PendingReceiverIROB empty_sentinel_irob;
};

template <typename Predicate>
void
PendingReceiverIROBLattice::release_if_ready(PendingReceiverIROB *pirob,
                                             Predicate is_ready)
{
    if (is_ready(pirob)) {
        /* TODO: smarter strategy for ordering ready IROBs. */
        dbgprintf("Releasing IROB %ld\n", pirob->id);
        release(pirob->id, pirob->send_labels);
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
        //dependent->dep_satisfied(pirob->id); // now done in erase()
        release_if_ready(dependent, is_ready);
    }
}

class ReadyIROB {
  public:
    bool operator()(PendingReceiverIROB *pirob) {
        assert(pirob);
        return (pirob->is_complete() && pirob->is_ready());
    }
};

#endif
