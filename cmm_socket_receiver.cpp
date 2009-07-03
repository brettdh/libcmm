#include "cmm_socket_receiver.h"
#include <pthread.h>
#include "debug.h"

#include <stdexcept>

CMMSocketReceiver::CMMSocketReceiver(CMMSocketImpl *sk_)
    : sk(sk_)
{
    handle(CMM_CONTROL_MSG_BEGIN_IROB, &CMMSocketReceiver::do_begin_irob);
    handle(CMM_CONTROL_MSG_END_IROB, &CMMSocketReceiver::do_end_irob);
    handle(CMM_CONTROL_MSG_IROB_CHUNK, &CMMSocketReceiver::do_irob_chunk);
    handle(CMM_CONTROL_MSG_NEW_INTERFACE, 
           &CMMSocketReceiver::do_new_interface);
    handle(CMM_CONTROL_MSG_DOWN_INTERFACE, 
           &CMMSocketReceiver::do_down_interface);
    handle(CMM_CONTROL_MSG_ACK, &CMMSocketReceiver::do_ack);
}

void
CMMSocketReceiver::correct_deps(PendingIROB *pirob)
{
    /* 1) If pirob is anonymous, add deps on all pending IROBs. */
    /* 2) Otherwise, add deps on all pending anonymous IROBs. */
    for (PendingIROBHash::iterator it = pending_irobs.begin();
         it != pending_irobs.end(); it++) {
        if (pirob->is_anonymous() || it->second->is_anonymous()) {
            pirob->add_dep(it->first);
        }
    }
    /* 3) Remove already-satisfied deps. */
    pirob->remove_deps_if(bind1st(mem_fun_ref(&IntSet::contains), 
                                  committed_irobs));
}

void
CMMSocketReceiver::do_begin_irob(struct CMMSocketControlHdr hdr)
{
    assert(ntohs(hdr.type) == CMM_CONTROL_MSG_BEGIN_IROB);
    if (hdr.op.begin_irob.numdeps > 0) {
        assert(hdr.op.begin_irob.deps);
    }

    irob_id_t id = ntohl(hdr.op.begin_irob.id);
    if (committed_irobs.contains(id)) {
        throw CMMControlException("Tried to begin IROB that's been committed",
                                  hdr);
    }

    PendingIROBHash::accessor ac;
    if (!pending_irobs.insert(ac, id)) {
        throw CMMControlException("Tried to begin IROB that already exists", 
                                  hdr);
    }
    ac->second = new PendingIROB(hdr.op.begin_irob, this);
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
        if (committed_irobs.contains(id)) {
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
    /* TODO: signal threads waiting for incoming data? */

}

void
CMMSocketReceiver::do_irob_chunk(struct CMMSocketControlHdr hdr)
{
    assert(ntohs(hdr.type) == CMM_CONTROL_MSG_IROB_CHUNK);
    PendingIROBHash::accessor ac;
    irob_id_t id = ntohl(hdr.op.irob_chunk.id);
    if (!pending_irobs.find(ac, id)) {
        if (committed_irobs.contains(id)) {
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
    delete [] hdr.op.irob_chunk.data;
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
 *   and the committed_irobs set as IROBs are committed.
 * Thought: we could make this function read-only by using the
 *   msg_queue to cause the Receiver thread to do any needed updates.
 */
ssize_t
CMMSocketReceiver::recv(void *buf, size_t len, int flags)
{

}
