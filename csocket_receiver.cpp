#include "csocket_receiver.h"
#include "csocket.h"
#include "timeops.h"
#include "debug.h"
#include "pending_irob.h"
#include "pending_receiver_irob.h"
#include "cmm_socket.private.h"
#include "csocket_mapping.h"
#include "pthread_util.h"

CSocketReceiver::handler_fn_t CSocketReceiver::handlers[] = {
    &CSocketReceiver::unrecognized_control_msg, /* HELLO not expected */
    &CSocketReceiver::do_begin_irob,
    &CSocketReceiver::do_end_irob,
    &CSocketReceiver::do_irob_chunk,
    &CSocketReceiver::do_default_irob,
    &CSocketReceiver::do_new_interface,
    &CSocketReceiver::do_down_interface,
    &CSocketReceiver::do_ack,
    &CSocketReceiver::do_goodbye
};

CSocketReceiver::CSocketReceiver(CSocketPtr csock_) 
    : csock(csock_), sk(get_pointer(csock_->sk)) {}

void
CSocketReceiver::unrecognized_control_msg(struct CMMSocketControlHdr hdr)
{
    throw CMMControlException("Unrecognized control message", hdr);
}

void
CSocketReceiver::dispatch(struct CMMSocketControlHdr hdr)
{
    short type = ntohs(hdr.msgtype());
    if (type < 0 || type > CMM_CONTROL_MSG_GOODBYE) {
        unrecognized_control_msg(hdr);
    } else {
        (this->*handlers[type])(hdr);
    }
}

static ssize_t 
read_bytes(int sock, void *buf, size_t count)
{
    ssize_t bytes_read = 0;
    while (bytes_read < (ssize_t)count) {
	int rc = read(sock, (char*)buf + bytes_read, 
		      count - bytes_read);
	if (rc < 0) {
            return rc;
	} else if (rc == 0) {
	    return rc;
	}
	bytes_read += rc;
    }
    return bytes_read;
}

void
CSocketReceiver::Run(void)
{
    char name[MAX_NAME_LEN+1];
    memset(name, 0, MAX_NAME_LEN+1);
    snprintf(name, MAX_NAME_LEN, "CSockReceiver %d", csock->osfd);
    set_thread_name(name);

    while (1) {
        struct CMMSocketControlHdr hdr;

	dbgprintf("About to wait for network messages\n");

        int rc = read_bytes(csock->osfd, &hdr, sizeof(hdr));
        if (rc != sizeof(hdr)) {
	    dbgprintf("CSocketReceiver: recv failed, rc = %d, errno=%d\n", rc, errno);
            return;
        }

	dbgprintf("Received message: %s\n", hdr.describe().c_str());

        try {
            dispatch(hdr);
        } catch (CMMFatalError& e) {
            dbgprintf("Fatal error on multisocket %d: %s\n",
                      csock->osfd, e.hdr.describe().c_str());
            sk->goodbye(false);
            throw;
        }
    }
}

void
CSocketReceiver::Finish(void)
{
    {
        PthreadScopedLock lock(&sk->scheduling_state_lock);
        csock->csock_recvr = NULL;
        //sk->incoming_irobs.shutdown();
        //csock->remove();
        pthread_cond_broadcast(&sk->scheduling_state_cv);
    }

    dbgprintf("Exiting.\n");

    // nobody will pthread_join to the receiver now, so detach
    //  to make sure the memory gets reclaimed
    detach();

    delete this; // the last thing that will ever be done with this
}

static irob_id_t *
read_deps_array(int fd, int numdeps, struct CMMSocketControlHdr& hdr)
{
    irob_id_t *deps = NULL;
    if (numdeps > 0) {
        deps = new irob_id_t[numdeps];
        int datalen = numdeps * sizeof(irob_id_t);
        int rc = read_bytes(fd, (char*)deps, datalen);
        if (rc != datalen) {
            if (rc < 0) {
                dbgprintf("Error %d on socket %d\n", errno, fd);
            } else {
                dbgprintf("Expected %d bytes after header, received %d\n", 
                          datalen, rc);
            }
            delete [] deps;
            throw CMMControlException("Socket error", hdr);
        }

        for (int i = 0; i < numdeps; ++i) {
            deps[i] = ntohl(deps[i]);
        }
    }
    return deps;
}

static char *
read_data_buffer(int fd, int datalen, struct CMMSocketControlHdr& hdr)
{
    if (datalen <= 0) {
        throw CMMControlException("Expected data with header, got none", hdr);
    }
    
    char *buf = new char[datalen];
    int rc = read_bytes(fd, buf, datalen);
    if (rc != datalen) {
        if (rc < 0) {
            dbgprintf("Error %d on socket %d\n", errno, fd);
        } else {
            dbgprintf("Expected %d bytes after header, received %d\n", 
                      datalen, rc);
        }
        delete [] buf;
        throw CMMControlException("Socket error", hdr);
    }

    dbgprintf("Successfully got %d data bytes\n", datalen);
    return buf;
}

void CSocketReceiver::do_begin_irob(struct CMMSocketControlHdr hdr)
{
    struct timeval begin, end, diff;
    TIME(begin);

    assert(ntohs(hdr.type) == CMM_CONTROL_MSG_BEGIN_IROB);
    int numdeps = ntohl(hdr.op.begin_irob.numdeps);
    irob_id_t *deps = read_deps_array(csock->osfd, numdeps, hdr);

    irob_id_t id = ntohl(hdr.op.begin_irob.id);

    PendingIROB *pirob = new PendingReceiverIROB(id, numdeps, deps, 0, NULL,
						 ntohl(hdr.send_labels));
    
    {
        PthreadScopedLock lock(&sk->scheduling_state_lock);
        if (!sk->incoming_irobs.insert(pirob, false)) {
            delete pirob;
            throw CMMFatalError("Tried to begin IROB that already exists", 
                                hdr);
        }
    }

    if (numdeps > 0) {
        delete [] deps;
    }

    TIME(end);
    TIMEDIFF(begin, end, diff);
    dbgprintf("Receiver began IROB %d, took %lu.%06lu seconds\n",
	      id, diff.tv_sec, diff.tv_usec);
}

void
CSocketReceiver::do_end_irob(struct CMMSocketControlHdr hdr)
{
    struct timeval begin, end, diff;
    TIME(begin);
    assert(ntohs(hdr.type) == CMM_CONTROL_MSG_END_IROB);

    irob_id_t id = ntohl(hdr.op.end_irob.id);
    {
        PthreadScopedLock lock(&sk->scheduling_state_lock);
        PendingIROB *pirob = sk->incoming_irobs.find(id);
        if (!pirob) {
            if (sk->incoming_irobs.past_irob_exists(id)) {
                throw CMMFatalError("Tried to end committed IROB", hdr);
            } else {
                throw CMMFatalError("Tried to end nonexistent IROB", hdr);
            }
        }
        
        assert(pirob);
        if (!pirob->finish()) {
            throw CMMFatalError("Tried to end already-done IROB", hdr);
        }
        
        PendingReceiverIROB *prirob = dynamic_cast<PendingReceiverIROB*>(pirob);
        sk->incoming_irobs.release_if_ready(prirob, ReadyIROB());

        csock->irob_indexes.waiting_acks.insert(IROBSchedulingData(id));
        pthread_cond_broadcast(&sk->scheduling_state_cv);
    }

    //sk->sendr->ack(id);

    TIME(end);
    TIMEDIFF(begin, end, diff);
    dbgprintf("Receiver ended IROB %d, took %lu.%06lu seconds\n",
	      id, diff.tv_sec, diff.tv_usec);
}


void CSocketReceiver::do_irob_chunk(struct CMMSocketControlHdr hdr)
{
    struct timeval begin, end, diff;
    TIME(begin);

    assert(ntohs(hdr.type) == CMM_CONTROL_MSG_IROB_CHUNK);
    int datalen = ntohl(hdr.op.irob_chunk.datalen);
    char *buf = read_data_buffer(csock->osfd, datalen, hdr);

    hdr.op.irob_chunk.data = buf;

    irob_id_t id = ntohl(hdr.op.irob_chunk.id);

    {
        PthreadScopedLock lock(&sk->scheduling_state_lock);
        
        PendingIROB *pirob = sk->incoming_irobs.find(id);
        if (!pirob) {
            if (sk->incoming_irobs.past_irob_exists(id)) {
                throw CMMFatalError("Tried to add to committed IROB", hdr);
            } else {
                throw CMMFatalError("Tried to add to nonexistent IROB", hdr);
            }
        }
        struct irob_chunk_data chunk;
        chunk.id = id;
        chunk.seqno = ntohl(hdr.op.irob_chunk.seqno);
        chunk.datalen = ntohl(hdr.op.irob_chunk.datalen);
        chunk.data = hdr.op.irob_chunk.data;
        
        assert(pirob);
        PendingReceiverIROB *prirob = dynamic_cast<PendingReceiverIROB*>(pirob);
        assert(prirob);
        if (!prirob->add_chunk(chunk)) {
            throw CMMFatalError("Tried to add to completed IROB", hdr);
        } else {
            dbgprintf("Successfully added chunk %d to IROB %d\n",
                      chunk.seqno, id);
        }

//         IROBScheduingData sched_data(id, chunk.seqno);
//         csock->irob_indexes.waiting_acks.insert(sched_data);
//         pthread_cond_broadcast(&sk->scheduling_state_cv);
    }
    
    TIME(end);
    TIMEDIFF(begin, end, diff);
    dbgprintf("Added chunk %d in IROB %d, took %lu.%06lu seconds\n",
	      ntohl(hdr.op.irob_chunk.seqno), id, diff.tv_sec, diff.tv_usec);
}

void CSocketReceiver::do_default_irob(struct CMMSocketControlHdr hdr)
{
    struct timeval begin, end, diff;
    TIME(begin);

    assert(ntohs(hdr.type) == CMM_CONTROL_MSG_DEFAULT_IROB);

    int numdeps = ntohl(hdr.op.default_irob.numdeps);
    irob_id_t *deps = read_deps_array(csock->osfd, numdeps, hdr);

    int datalen = ntohl(hdr.op.default_irob.datalen);
    char *buf = read_data_buffer(csock->osfd, datalen, hdr);

    irob_id_t id = ntohl(hdr.op.default_irob.id);
    /* modify data structures */
    PendingReceiverIROB *pirob = new PendingReceiverIROB(id, numdeps, deps, datalen, buf,
                                                         ntohl(hdr.send_labels));
    {
        PthreadScopedLock lock(&sk->scheduling_state_lock);
        if (!sk->incoming_irobs.insert(pirob, false)) {
            delete pirob;
            throw CMMFatalError("Tried to add default IROB "
                                      "that already exists", 
                                      hdr);
        }
        
        //PendingReceiverIROB *prirob = dynamic_cast<PendingReceiverIROB*>(pirob);
        //assert(prirob);
        sk->incoming_irobs.release_if_ready(pirob, ReadyIROB());

        csock->irob_indexes.waiting_acks.insert(IROBSchedulingData(id));
        pthread_cond_broadcast(&sk->scheduling_state_cv);
    }

    TIME(end);
    TIMEDIFF(begin, end, diff);
    dbgprintf("Received default IROB %d, took %lu.%06lu seconds\n",
	      id, diff.tv_sec, diff.tv_usec);
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
    sk->ack_received(ntohl(hdr.op.ack.id));
    size_t num_acks = ntohl(hdr.op.ack.num_acks);
    if (num_acks > 0) {
        irob_id_t *acked_irobs = new irob_id_t[num_acks];
        int datalen = num_acks * sizeof(irob_id_t);
        int rc = read_bytes(csock->osfd, (char*)acked_irobs, datalen);
        if (rc != datalen) {
            if (rc < 0) {
                dbgprintf("Error %d on socket %d\n", errno, csock->osfd);
            } else {
                dbgprintf("Expected %d bytes after header, received %d\n", 
                          datalen, rc);
            }
            delete [] acked_irobs;
            throw CMMControlException("Socket error", hdr);
        }

        for (size_t i = 0; i < num_acks; i++) {
            sk->ack_received(ntohl(acked_irobs[i]));
        }
        delete [] acked_irobs;
    }

    TIME(end);
    TIMEDIFF(begin, end, diff);
    dbgprintf("Receiver got ACK for IROB %d and %u others, took %lu.%06lu seconds\n",
	      ntohl(hdr.op.ack.id), num_acks,
	      diff.tv_sec, diff.tv_usec);
}

void
CSocketReceiver::do_goodbye(struct CMMSocketControlHdr hdr)
{
    assert(ntohs(hdr.type) == CMM_CONTROL_MSG_GOODBYE);
    if (sk->is_shutting_down()) {
	/* I initiated the shutdown; this is the "FIN/ACK" */
	/* at this point, both sides have stopped sending. */
	sk->goodbye_acked();
        throw CMMThreadFinish();
    } else {
	/* The other side initiated the shutdown; this is the "FIN" */
	/* Send the "FIN/ACK" */
	sk->goodbye(true);
    }
    /* Note: the senders will still wait for all waiting ACKs
     * to be sent before finalizing the shutdown. */
}
