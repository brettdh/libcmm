#include "csocket_receiver.h"
#include "timeops.h"
#include "debug.h"

class ReadyIROB {
  public:
    bool operator()(PendingIROB *pi) {
        assert(pi);
        PendingReceiverIROB *pirob = dynamic_cast<PendingReceiverIROB*>(pi);
        assert(pirob);
        return (pirob->is_complete() && pirob->is_released());
    }
};

CSocketReceiver::handler_fn_t CSocketReceiver::handlers[] = {
    &CSocketReceiver::unexpected_control_msg, /* HELLO not expected */
    &CSocketReceiver::do_begin_irob,
    &CSocketReceiver::do_end_irob,
    &CSocketReceiver::do_irob_chunk,
    &CSocketReceiver::do_default_irob,
    &CSocketReceiver::do_new_interface,
    &CSocketReceiver::do_down_interface,
    &CSocketReceiver::do_ack,
    &CSocketReceiver::do_goodbye
};

void
CSocketReceiver::dispatch(struct CMMSocketControlHdr hdr)
{
    short type = ntohs(hdr.msgtype());
    if (type < 0 || type > CMM_CONTROL_MSG_GOODBYE) {
        unexpected_control_msg(hdr);
    } else {
        (this->*handlers[type])(msg);
    }
}

void
CSocketReceiver::Run(void)
{
    while (1) {
        struct CMMSocketControlHdr hdr;

	dbgprintf("About to wait for network messages\n");
#if 0
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(csock->osfd, &readfds);
        int rc = select(csock->osfd + 1, &readfds, NULL, NULL, NULL);
        if (rc < 0) {
            if (errno == EINTR) {
                continue;
            } else {
		dbgprintf("CSocketReceiver: select failed, errno=%d\n", errno);
                return;
            }
        }

	dbgprintf("Network message incoming\n");
#endif

	struct timeval begin, end, diff;
	TIME(begin);

        int rc = recv(csock->osfd, &hdr, sizeof(hdr), 0);
        if (rc != sizeof(hdr)) {
	    dbgprintf("CSocketReceiver: recv failed, rc = %d, errno=%d\n", rc, errno);
            return;
        }

	dbgprintf("Received message: %s\n", hdr.describe().c_str());

        dispatch(hdr);

	TIME(end);
	TIMEDIFF(begin, end, diff);
	dbgprintf("Worker-receiver passed message in %lu.%06lu seconds (%s)\n",
		  diff.tv_sec, diff.tv_usec, hdr.describe().c_str());
    }
}

void
CSocketReceiver::Finish(void)
{
    /* Whether CSocketSender or CSocketReceiver gets here 
     * first, the result should be correct.
     * Everything should only get deleted once, and the
     * thread-stopping functions are idempotent. */
    csock->remove();
}

void CSocketReceiver::do_begin_irob(struct CMMSocketControlHdr hdr)
{
    struct timeval begin, end, diff;
    TIME(begin);

    assert(ntohs(hdr.type) == CMM_CONTROL_MSG_BEGIN_IROB);
    int numdeps = ntohl(hdr.op.begin_irob.numdeps);
    if (numdeps > 0) {
        irob_id_t *deps = new irob_id_t[numdeps];
        int datalen = numdeps * sizeof(irob_id_t);
        int rc = recv(csock->osfd, (char*)deps, datalen, 0);
        if (rc != datalen) {
            if (rc < 0) {
                dbgprintf("Error %d on socket %d\n", errno, csock->osfd);
            } else {
                dbgprintf("Expected %d bytes after header, received %d\n", 
                          datalen, rc);
            }
            delete [] deps;
            throw Exception::make("Socket error", hdr);
        }
        hdr.op.begin_irob.deps = deps;
    }

    hdr.op.begin_irob.id = ntohl(hdr.op.begin_irob.id);
    hdr.op.begin_irob.numdeps = ntohl(hdr.op.begin_irob.numdeps);
    PendingIROB *pirob = new PendingReceiverIROB(hdr.op.begin_irob,
						 ntohl(hdr.send_labels),
						 ntohl(hdr.recv_labels));
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
	      hdr.op.begin_irob.id, diff.tv_sec, diff.tv_usec);
}

void
CSocketReceiver::do_end_irob(struct CMMSocketControlHdr hdr)
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

    PendingReceiverIROB *prirob = dynamic_cast<PendingReceiverIROB*>(pirob);
    pending_irobs.release_if_ready(prirob, ReadyIROB());
    ac.release();

    //sk->sendr->ack(id);

    TIME(end);
    TIMEDIFF(begin, end, diff);
    dbgprintf("Receiver ended IROB %d, took %lu.%06lu seconds\n",
	      id, diff.tv_sec, diff.tv_usec);
}


void CSocketReceiver::do_irob_chunk(struct CMMSocketControlHdr hdr)
{
    assert(ntohs(hdr.type) == CMM_CONTROL_MSG_IROB_CHUNK);
    int datalen = ntohl(hdr.op.irob_chunk.datalen);
    if (datalen <= 0) {
        throw Exception::make("Expected data with header, got none", hdr);
    }
    
    char *buf = new char[datalen];
    int rc = recv(csock->osfd, buf, datalen, MSG_WAITALL);
    if (rc != datalen) {
        if (rc < 0) {
            dbgprintf("Error %d on socket %d\n", errno, csock->osfd);
        } else {
            dbgprintf("Expected %d bytes after header, received %d\n", 
                      datalen, rc);
        }
        delete [] buf;
        throw Exception::make("Socket error", hdr);
    }
    dbgprintf("Successfully got %d data bytes\n", datalen);

    hdr.op.irob_chunk.data = buf;

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
    PendingReceiverIROB *prirob = dynamic_cast<PendingReceiverIROB*>(pirob);
    assert(prirob);
    if (!prirob->add_chunk(chunk)) {
        throw Exception::make("Tried to add to completed IROB", hdr);
    } else {
	dbgprintf("Successfully added chunk %d to IROB %d\n",
		  chunk.seqno, id);
    }
    ac.release();
    u_long ack_send_labels = ntohl(hdr.recv_labels);
    u_long ack_recv_labels = ntohl(hdr.send_labels);
    
    sk->sendr->ack(id, chunk.seqno, ack_send_labels, ack_recv_labels);
    TIME(end);
    TIMEDIFF(begin, end, diff);
    dbgprintf("Added and ACK'd chunk %d in IROB %d, took %lu.%06lu seconds\n",
	      chunk.seqno, id, diff.tv_sec, diff.tv_usec);
}

void CSocketReceiver::do_default_irob(struct CMMSocketControlHdr hdr)
{
    struct timeval begin, end, diff;
    TIME(begin);

    assert(ntohs(hdr.type) == CMM_CONTROL_MSG_DEFAULT_IROB);
    int datalen = ntohl(hdr.op.default_irob.datalen);
    if (datalen <= 0) {
        throw Exception::make("Expected data with header, got none", hdr);
    }
    
    char *buf = new char[datalen];
    int rc = recv(csock->osfd, buf, datalen, MSG_WAITALL);
    if (rc != datalen) {
        if (rc < 0) {
            dbgprintf("Error %d on socket %d\n", errno, csock->osfd);
        } else {
            dbgprintf("Expected %d bytes after header, received %d\n", 
                      datalen, rc);
        }
        delete [] buf;
        throw Exception::make("Socket error", hdr);
    }
    dbgprintf("Successfully got %d data bytes\n", datalen);

    hdr.op.default_irob.data = buf;

    hdr.op.default_irob.id = ntohl(hdr.op.default_irob.id);
    hdr.op.default_irob.datalen = ntohl(hdr.op.default_irob.datalen);
    /* modify data structures */
    PendingIROB *pirob = new PendingReceiverIROB(hdr.op.default_irob,
						 ntohl(hdr.send_labels),
						 ntohl(hdr.recv_labels));
    PendingIROBHash::accessor ac;
    if (!pending_irobs.insert(ac, pirob)) {
        delete pirob;
        throw Exception::make("Tried to add default IROB that already exists", 
			      hdr);
    }
    
    PendingReceiverIROB *prirob = dynamic_cast<PendingReceiverIROB*>(pirob);
    pending_irobs.release_if_ready(prirob, ReadyIROB());
    ac.release();

    u_long ack_send_labels = ntohl(hdr.recv_labels);
    u_long ack_recv_labels = ntohl(hdr.send_labels);

    sk->sendr->ack(hdr.op.default_irob.id, INVALID_IROB_SEQNO,
		   ack_send_labels, ack_recv_labels);
    TIME(end);
    TIMEDIFF(begin, end, diff);
    dbgprintf("Received default IROB %d, took %lu.%06lu seconds\n",
	      hdr.op.default_irob.id, diff.tv_sec, diff.tv_usec);
}

void
CSocketReceiver::do_new_interface(struct CMMSocketControlHdr hdr)
{
    assert(ntohs(hdr.type) == CMM_CONTROL_MSG_NEW_INTERFACE);
    struct net_interface iface = {hdr.op.new_interface.ip_addr,
                                  hdr.op.new_interface.labels};
    sk->setup(iface, false);
}

void
CSocketReceiver::do_down_interface(struct CMMSocketControlHdr hdr)
{
    assert(ntohs(hdr.type) == CMM_CONTROL_MSG_DOWN_INTERFACE);
    struct net_interface iface = {hdr.op.down_interface.ip_addr};
    sk->teardown(iface, false);
}

void
CSocketReceiver::do_ack(struct CMMSocketControlHdr hdr)
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
CSocketReceiver::do_goodbye(struct CMMSocketControlHdr hdr)
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
