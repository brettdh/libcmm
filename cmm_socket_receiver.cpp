#include "cmm_socket_receiver.h"
#include <pthread.h>
#include "debug.h"

class ReadyIROB : public PendingIROBLattice::Predicate {
  public:
    bool operator()(PendingIROB *pi) {
        assert(pi);
        PendingReceiverIROB *pirob = static_cast<PendingReceiverIROB*>(pi);
        assert(pirob);
        return (pirob->is_complete() && pirob->is_released());
    }
};

CMMSocketReceiver::CMMSocketReceiver(CMMSocketImpl *sk_)
    : sk(sk_), pending_irobs(ReadyIROB())
{
    handle(CMM_CONTROL_MSG_BEGIN_IROB, this, 
           &CMMSocketReceiver::do_begin_irob);
    handle(CMM_CONTROL_MSG_END_IROB, this, &CMMSocketReceiver::do_end_irob);
    handle(CMM_CONTROL_MSG_IROB_CHUNK, this,
           &CMMSocketReceiver::do_irob_chunk);
    handle(CMM_CONTROL_MSG_NEW_INTERFACE, this, 
           &CMMSocketReceiver::do_new_interface);
    handle(CMM_CONTROL_MSG_DOWN_INTERFACE, this, 
           &CMMSocketReceiver::do_down_interface);
    handle(CMM_CONTROL_MSG_ACK, this, &CMMSocketReceiver::do_ack);
}

CMMSocketReceiver::~CMMSocketReceiver()
{
    PendingIROBHash::accessor ac;
    while (pending_irobs.find(ac, TruePred())) {
        PendingIROB *victim = ac->second;
        pending_irobs.erase(ac);
        delete victim;
    }
}

void
CMMSocketReceiver::do_begin_irob(struct CMMSocketControlHdr hdr)
{
    assert(ntohs(hdr.type) == CMM_CONTROL_MSG_BEGIN_IROB);
    if (hdr.op.begin_irob.numdeps > 0) {
        assert(hdr.op.begin_irob.deps);
    }

    PendingIROB *pirob = new PendingReceiverIROB(hdr.op.begin_irob);
    PendingIROBHash::accessor ac;
    if (!pending_irobs.insert(ac, pirob)) {
        delete pirob;
        throw CMMControlException("Tried to begin IROB that already exists", 
                                  hdr);
    }

    if (hdr.op.begin_irob.numdeps > 0) {
        delete [] hdr.op.begin_irob.deps;
    }
}

void
CMMSocketReceiver::do_end_irob(struct CMMSocketControlHdr hdr)
{
    assert(ntohs(hdr.type) == CMM_CONTROL_MSG_END_IROB);
    PendingIROBHash::accessor ac;
    irob_id_t id = ntohl(hdr.op.end_irob.id);
    if (!pending_irobs.find(ac, id)) {
        if (pending_irobs.past_irob_exists(id)) {
            throw CMMControlException("Tried to end committed IROB", hdr);
        } else {
            throw CMMControlException("Tried to end nonexistent IROB", hdr);
        }
    }
    PendingIROB *pirob = ac->second;
    assert(pirob);
    if (!pirob->finish()) {
        throw CMMControlException("Tried to end already-done IROB", hdr);
    }

    wake_up_readers();
}

void
CMMSocketReceiver::do_irob_chunk(struct CMMSocketControlHdr hdr)
{
    assert(ntohs(hdr.type) == CMM_CONTROL_MSG_IROB_CHUNK);
    PendingIROBHash::accessor ac;
    irob_id_t id = ntohl(hdr.op.irob_chunk.id);
    if (!pending_irobs.find(ac, id)) {
        if (pending_irobs.past_irob_exists(id)) {
            throw CMMControlException("Tried to add to committed IROB", hdr);
        } else {
            throw CMMControlException("Tried to add to nonexistent IROB", hdr);
        }
    }
    PendingIROB *pirob = ac->second;
    assert(pirob);
    if (!pirob->add_chunk(hdr.op.irob_chunk)) {
        throw CMMControlException("Tried to add to completed IROB", hdr);
    }
}

void
CMMSocketReceiver::do_new_interface(struct CMMSocketControlHdr hdr)
{
    assert(ntohs(hdr.type) == CMM_CONTROL_MSG_NEW_INTERFACE);
    sk->setup(hdr.op.new_interface, false);
}

void
CMMSocketReceiver::do_down_interface(struct CMMSocketControlHdr hdr)
{
    assert(ntohs(hdr.type) == CMM_CONTROL_MSG_DOWN_INTERFACE);
    sk->teardown(hdr.op.down_interface, false);
}

void
CMMSocketReceiver::do_ack(struct CMMSocketControlHdr hdr)
{
    assert(ntohs(hdr.type) == CMM_CONTROL_MSG_ACK);
    sk->sendr->ack_received(ntohl(hdr.op.ack.id), ntohl(hdr.op.ack.seqno));
}


/* This is where all the scheduling logic happens. 
 * This function decides how to pass IROB data to the application. 
 *
 * Additionally, this function is the only place we need
 * to worry about race conditions with the Receiver thread.
 * Thus, the locking discipline of that thread will be informed
 * by whatever reads and writes are necessary here.
 * 
 * Initial stab at requirements: 
 *   This thread definitely needs to read from the pending_irobs
 *   data structure, to figure out what data to pass to the
 *   application.  It also needs to update this data structure
 *   and the past_irobs set as IROBs are committed.
 * Thought: we could make this function read-only by using the
 *   msg_queue to cause the Receiver thread to do any needed updates.
 */
/* TODO: nonblocking mode */
ssize_t
CMMSocketReceiver::recv(void *buf, size_t len, int flags, u_long *recv_labels)
{
    PendingReceiverIROB *pirob = get_next_irob();
    assert(pirob); /* nonblocking version would return NULL */
    assert(pirob->is_released());
    assert(pirob->is_complete()); /* XXX: see get_next_irob */

    vector<PendingReceiverIROB *pirob> pirobs;
    
    ssize_t bytes_ready = pirob->numbytes();
    assert(bytes_ready > 0);
    while (bytes_ready < len) {
        pirobs.push_back(pirob);

        pirob = get_next_irob();
        assert(pirob->is_released());
        assert(pirob->is_complete()); /* XXX: see get_next_irob */

        ssize_t bytes = pirob->numbytes();
        assert(bytes > 0);
        bytes_ready += bytes;
    }
    pirobs.push_back(pirob);

    ssize_t bytes_passed = 0;
    for (size_t i = 0; i < pirobs.size(); i++) {
        PendingIROBHash::accessor ac;
        if (!pending_irobs.find(ac, pirob->id)) {
            assert(0);
        }
        assert(pirob == ac->second);

        bytes_passed += pirob->read_data(buf + bytes_passed,
                                         len - bytes_passed);
        if (pirob->is_complete() && pirob->numbytes() == 0) {
            pending_irobs.erase(ac);
            pending_irobs.release_dependents(pirob);
            delete pirob;
        } else {
            /* this should be true for at most the last IROB
             * in the vector */
            pending_irobs.partially_read(pirob);
        }
    }
    /* TODO: pass recv_labels */
    return bytes_passed;
}

/* First take: this won't ever return an incomplete IROB. 
 *  (we may want to loosen this restriction in the future) */
/* Hard rule: this won't ever return an unreleased IROB. */
PendingReceiverIROB *
CMMSocketReceiver::get_next_irob()
{
    return pending_irobs.get_ready_irob();
}
