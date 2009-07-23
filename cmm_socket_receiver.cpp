#include "cmm_socket_receiver.h"
#include "cmm_socket_sender.h"
#include <pthread.h>
#include "debug.h"
#include "timeops.h"
#include <vector>
using std::vector;

class ReadyIROB {
  public:
    bool operator()(PendingIROB *pi) {
        assert(pi);
        PendingReceiverIROB *pirob = static_cast<PendingReceiverIROB*>(pi);
        assert(pirob);
        return (pirob->is_complete() && pirob->is_released());
    }
};

CMMSocketReceiver::CMMSocketReceiver(CMMSocketImpl *sk_)
    : sk(sk_)
{
    handle(CMM_CONTROL_MSG_BEGIN_IROB, this, 
           &CMMSocketReceiver::do_begin_irob);
    handle(CMM_CONTROL_MSG_END_IROB, this, &CMMSocketReceiver::do_end_irob);
    handle(CMM_CONTROL_MSG_IROB_CHUNK, this,
           &CMMSocketReceiver::do_irob_chunk);
    handle(CMM_CONTROL_MSG_DEFAULT_IROB, this,
           &CMMSocketReceiver::do_default_irob);
    handle(CMM_CONTROL_MSG_NEW_INTERFACE, this, 
           &CMMSocketReceiver::do_new_interface);
    handle(CMM_CONTROL_MSG_DOWN_INTERFACE, this, 
           &CMMSocketReceiver::do_down_interface);
    handle(CMM_CONTROL_MSG_ACK, this, &CMMSocketReceiver::do_ack);
    handle(CMM_CONTROL_MSG_GOODBYE, this, &CMMSocketReceiver::do_goodbye);
}

CMMSocketReceiver::~CMMSocketReceiver()
{
    stop();

    PendingIROBHash::accessor ac;
    while (pending_irobs.any(ac)) {
        PendingIROB *victim = ac->second;
        pending_irobs.erase(ac);
        delete victim;
        ac.release();
    }
}

void
CMMSocketReceiver::dispatch(struct CMMSocketControlHdr hdr)
{
    struct timeval now;
    TIME(now);
    dbgprintf("Receiver-scheduler got request: %s\n", 
	      hdr.describe().c_str());
    CMMSocketScheduler<struct CMMSocketControlHdr>::dispatch(hdr);
}

void
CMMSocketReceiver::do_begin_irob(struct CMMSocketControlHdr hdr)
{
    struct timeval begin, end, diff;
    TIME(begin);
    assert(ntohs(hdr.type) == CMM_CONTROL_MSG_BEGIN_IROB);
    if (hdr.op.begin_irob.numdeps > 0) {
        assert(hdr.op.begin_irob.deps);
    }

    PendingIROB *pirob = new PendingReceiverIROB(hdr.op.begin_irob);
    PendingIROBHash::accessor ac;
    if (!pending_irobs.insert(ac, pirob)) {
        delete pirob;
        throw Exception::make("Tried to begin IROB that already exists", 
                                  hdr);
    }

    if (hdr.op.begin_irob.numdeps > 0) {
        delete [] hdr.op.begin_irob.deps;
    }
    TIME(end);
    TIMEDIFF(begin, end, diff);
    dbgprintf("Receiver began IROB %d, took %lu.%06lu seconds\n",
	      ntohl(hdr.op.begin_irob.id), diff.tv_sec, diff.tv_usec);
}

void
CMMSocketReceiver::do_end_irob(struct CMMSocketControlHdr hdr)
{
    struct timeval begin, end, diff;
    TIME(begin);
    assert(ntohs(hdr.type) == CMM_CONTROL_MSG_END_IROB);
    PendingIROBHash::accessor ac;
    irob_id_t id = ntohl(hdr.op.end_irob.id);
    if (!pending_irobs.find(ac, id)) {
        if (pending_irobs.past_irob_exists(id)) {
            throw Exception::make("Tried to end committed IROB", hdr);
        } else {
            throw Exception::make("Tried to end nonexistent IROB", hdr);
        }
    }
    PendingIROB *pirob = ac->second;
    assert(pirob);
    if (!pirob->finish()) {
        throw Exception::make("Tried to end already-done IROB", hdr);
    }

    PendingReceiverIROB *prirob = static_cast<PendingReceiverIROB*>(pirob);
    pending_irobs.release_if_ready(prirob, ReadyIROB());
    ac.release();

    //sk->sendr->ack(id);

    TIME(end);
    TIMEDIFF(begin, end, diff);
    dbgprintf("Receiver ended IROB %d, took %lu.%06lu seconds\n",
	      id, diff.tv_sec, diff.tv_usec);
}

void
CMMSocketReceiver::do_irob_chunk(struct CMMSocketControlHdr hdr)
{
    struct timeval begin, end, diff;
    TIME(begin);
    assert(ntohs(hdr.type) == CMM_CONTROL_MSG_IROB_CHUNK);
    irob_id_t id = ntohl(hdr.op.irob_chunk.id);
    PendingIROBHash::accessor ac;
    if (!pending_irobs.find(ac, id)) {
        if (pending_irobs.past_irob_exists(id)) {
            throw Exception::make("Tried to add to committed IROB", hdr);
        } else {
            throw Exception::make("Tried to add to nonexistent IROB", hdr);
        }
    }
    struct irob_chunk_data chunk;
    chunk.id = id;
    chunk.seqno = ntohl(hdr.op.irob_chunk.seqno);
    chunk.datalen = ntohl(hdr.op.irob_chunk.datalen);
    chunk.data = hdr.op.irob_chunk.data;

    PendingIROB *pirob = ac->second;
    assert(pirob);
    PendingReceiverIROB *prirob = static_cast<PendingReceiverIROB*>(pirob);
    assert(prirob);
    if (!prirob->add_chunk(chunk)) {
        throw Exception::make("Tried to add to completed IROB", hdr);
    } else {
	dbgprintf("Successfully added chunk %d to IROB %d\n",
		  chunk.seqno, id);
    }
    ac.release();
    
    sk->sendr->ack(id, chunk.seqno);
    TIME(end);
    TIMEDIFF(begin, end, diff);
    dbgprintf("Added and ACK'd chunk %d in IROB %d, took %lu.%06lu seconds\n",
	      chunk.seqno, id, diff.tv_sec, diff.tv_usec);
}

void
CMMSocketReceiver::do_default_irob(struct CMMSocketControlHdr hdr)
{
    struct timeval begin, end, diff;
    TIME(begin);

    assert(ntohs(hdr.type) == CMM_CONTROL_MSG_DEFAULT_IROB);
    PendingIROB *pirob = new PendingReceiverIROB(hdr.op.default_irob);
    PendingIROBHash::accessor ac;
    if (!pending_irobs.insert(ac, pirob)) {
        delete pirob;
        throw Exception::make("Tried to add default IROB that already exists", 
			      hdr);
    }
    
    TIME(end);
    TIMEDIFF(begin, end, diff);
    dbgprintf("Sent default IROB %d, took %lu.%06lu seconds\n",
	      ntohl(hdr.op.default_irob.id), diff.tv_sec, diff.tv_usec);
}

void
CMMSocketReceiver::do_new_interface(struct CMMSocketControlHdr hdr)
{
    assert(ntohs(hdr.type) == CMM_CONTROL_MSG_NEW_INTERFACE);
    struct net_interface iface = {hdr.op.new_interface.ip_addr,
                                  hdr.op.new_interface.labels};
    sk->setup(iface, false);
}

void
CMMSocketReceiver::do_down_interface(struct CMMSocketControlHdr hdr)
{
    assert(ntohs(hdr.type) == CMM_CONTROL_MSG_DOWN_INTERFACE);
    struct net_interface iface = {hdr.op.down_interface.ip_addr};
    sk->teardown(iface, false);
}

void
CMMSocketReceiver::do_ack(struct CMMSocketControlHdr hdr)
{
    struct timeval begin, end, diff;
    TIME(begin);

    assert(ntohs(hdr.type) == CMM_CONTROL_MSG_ACK);
    sk->sendr->ack_received(ntohl(hdr.op.ack.id), ntohl(hdr.op.ack.seqno));

    TIME(end);
    TIMEDIFF(begin, end, diff);
    dbgprintf("Receiver got ACK for IROB %d chunk %d, took %lu.%06lu seconds\n",
	      ntohl(hdr.op.ack.id), ntohl(hdr.op.ack.seqno), 
	      diff.tv_sec, diff.tv_usec);
}

void
CMMSocketReceiver::do_goodbye(struct CMMSocketControlHdr hdr)
{
    assert(ntohs(hdr.type) == CMM_CONTROL_MSG_GOODBYE);
    if (sk->sendr->is_shutting_down()) {
	/* I initiated the shutdown; this is the "FIN/ACK" */
	/* at this point, both sides have stopped sending. */
	sk->sendr->goodbye_acked();
    } else {
	/* The other side initiated the shutdown; this is the "FIN" */
	/* Send the "FIN/ACK" */
	sk->sendr->goodbye(true);
    }
    /* Note: the senders will still wait for all IROB chunks
     * to be ACK'd before finalizing the shutdown. */
}

void
CMMSocketReceiver::shutdown()
{
    pending_irobs.shutdown();
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
CMMSocketReceiver::recv(void *bufp, size_t len, int flags, u_long *recv_labels)
{
    vector<PendingReceiverIROB *> pirobs;
    char *buf = (char*)bufp;
    
    struct timeval begin, end, diff;
    TIME(begin);

    ssize_t bytes_ready = 0;
    while ((size_t)bytes_ready < len) {
	struct timeval one_begin, one_end, one_diff;
	TIME(one_begin);
        PendingReceiverIROB *pirob = pending_irobs.get_ready_irob();
	TIME(one_end);
	TIMEDIFF(one_begin, one_end, one_diff);
	dbgprintf("Getting one ready IROB took %lu.%06lu seconds\n",
		  one_diff.tv_sec, one_diff.tv_usec);
	if (!pirob) {
	    if (sk->sendr->is_shutting_down()) {
		return 0;
	    } else {
		assert(0); /* XXX: nonblocking case may return NULL */
	    }
	}

        /* after the IROB is returned here, no other thread will
         * unsafely modify it.
         * XXX: this will not be true if we allow get_next_irob
         * to return released, incomplete IROBs.
         * We could fix that by simply having a sentinel chunk
         * on the concurrent_queue of chunks. */

        assert(pirob->is_released());
        assert(pirob->is_complete()); /* XXX: see get_next_irob */

        ssize_t bytes = pirob->numbytes();
        assert(bytes > 0);
        bytes_ready += bytes;

        pirobs.push_back(pirob);

        if (!pirob->is_complete()) {
            break;
        }
    }
    TIME(end);
    TIMEDIFF(begin, end, diff);
    dbgprintf("recv: gathering bytes took %lu.%06lu seconds\n", 
	      diff.tv_sec, diff.tv_usec);

    TIME(begin);

    ssize_t bytes_passed = 0;
    bool partial_irob = false;
    for (size_t i = 0; i < pirobs.size(); i++) {
        PendingIROBHash::accessor ac;
        PendingReceiverIROB *pirob = pirobs[i];
        if (!pending_irobs.find(ac, pirob->id)) {
            assert(0);
        }
        assert(pirob == ac->second);
        if (i == 0) {
            if (recv_labels) {
                *recv_labels = pirob->send_labels;
            }
        }

        bytes_passed += pirob->read_data(buf + bytes_passed,
                                         len - bytes_passed);
        if (pirob->is_complete() && pirob->numbytes() == 0) {
            pending_irobs.erase(ac);
            pending_irobs.release_dependents(pirob, ReadyIROB());
            delete pirob;
        } else {
            if (!pirob->is_complete()) {
                /* This should still be the last one in the list,
                 * since it MUST finish before any IROB can be
                 * passed to the application. */
            }
            /* this should be true for at most the last IROB
             * in the vector */
            assert(!partial_irob);
            partial_irob = true;
            pending_irobs.partially_read(pirob);
        }
    }

    TIME(end);
    TIMEDIFF(begin, end, diff);
    dbgprintf("recv: Copying bytes took %lu.%06lu seconds\n",
	      diff.tv_sec, diff.tv_usec);
    dbgprintf("Passing bytes to application\n");
    return bytes_passed;
}

bool 
CMMSocketReceiver::find_irob(PendingIROBHash::const_accessor& ac, 
			     irob_id_t id)
{
    return pending_irobs.find(ac, id);
}
