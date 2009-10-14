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

    //&CSocketReceiver::do_default_irob,
    &CSocketReceiver::unrecognized_control_msg, /* DEFAULT_IROB not expected */

    &CSocketReceiver::do_new_interface,
    &CSocketReceiver::do_down_interface,
    &CSocketReceiver::do_ack,
    &CSocketReceiver::do_goodbye,
    &CSocketReceiver::do_request_resend
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
    if (type < 0 || type >= CMM_CONTROL_MSG_INVALID) {
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
                      sk->sock, e.what());
            sk->goodbye(false);
            shutdown(sk->select_pipe[1], SHUT_RDWR);
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

irob_id_t *
CSocketReceiver::read_deps_array(irob_id_t id, int numdeps, 
                                 struct CMMSocketControlHdr& hdr)
{
    irob_id_t *deps = NULL;
    if (numdeps > 0) {
        deps = new irob_id_t[numdeps];
        int datalen = numdeps * sizeof(irob_id_t);
        int rc = read_bytes(csock->osfd, (char*)deps, datalen);
        if (rc != datalen) {
            if (rc < 0) {
                dbgprintf("Error %d on socket %d\n", errno, csock->osfd);
            } else {
                dbgprintf("Expected %d bytes after header, received %d\n", 
                          datalen, rc);
            }
            delete [] deps;

            IROBSchedulingData data(id, CMM_RESEND_REQUEST_DEPS);
            csock->irob_indexes.resend_requests.insert(data);
            throw CMMControlException("Socket error", hdr);
        }

        for (int i = 0; i < numdeps; ++i) {
            deps[i] = ntohl(deps[i]);
        }
    }
    return deps;
}

char *
CSocketReceiver::read_data_buffer(irob_id_t id, int datalen,
                                  struct CMMSocketControlHdr& hdr)
{
    if (datalen <= 0) {
        throw CMMControlException("Expected data with header, got none", hdr);
    }
    
    char *buf = new char[datalen];
    int rc = read_bytes(csock->osfd, buf, datalen);
    if (rc != datalen) {
        if (rc < 0) {
            dbgprintf("Error %d on socket %d\n", errno, csock->osfd);
        } else {
            dbgprintf("Expected %d bytes after header, received %d\n", 
                      datalen, rc);
        }
        delete [] buf;
        
        IROBSchedulingData data(id, CMM_RESEND_REQUEST_DATA);
        csock->irob_indexes.resend_requests.insert(data);
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
    irob_id_t id = ntohl(hdr.op.begin_irob.id);
    int numdeps = ntohl(hdr.op.begin_irob.numdeps);
    irob_id_t *deps = read_deps_array(id, numdeps, hdr);


    PendingIROB *pirob = new PendingReceiverIROB(id, numdeps, deps, 0, NULL,
						 ntohl(hdr.send_labels));
    
    {
        PthreadScopedLock lock(&sk->scheduling_state_lock);
        if (!sk->incoming_irobs.insert(pirob, false)) {
            delete pirob;
            //throw CMMFatalError("Tried to begin committed IROB", hdr);
            dbgprintf("do_begin_irob: duplicate IROB %d, ignoring\n", id);
            return;
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

        bool resend_request = false;
        resend_request_type_t req_type = CMM_RESEND_REQUEST_NONE;

        PendingIROB *pirob = sk->incoming_irobs.find(id);
        if (!pirob) {
            if (sk->incoming_irobs.past_irob_exists(id)) {
                //throw CMMFatalError("Tried to end committed IROB", hdr);
                dbgprintf("do_end_irob: previously-finished IROB %d, resending ACK\n", id);
                sk->irob_indexes.waiting_acks.insert(IROBSchedulingData(id, false));
                pthread_cond_broadcast(&sk->scheduling_state_cv);
                return;
            } else {
                //throw CMMFatalError("Tried to end nonexistent IROB", hdr);
                dbgprintf("Receiver got End_IROB for IROB %d; "
                          "creating placeholder\n", id);
                pirob = sk->incoming_irobs.make_placeholder(id);
                bool ret = sk->incoming_irobs.insert(pirob, false);
                assert(ret); // since it was absent before now
                
                resend_request = true;
                req_type = CMM_RESEND_REQUEST_BOTH;
            }
        }
        
        ssize_t expected_bytes = ntohl(hdr.op.end_irob.expected_bytes);

        assert(pirob);
        PendingReceiverIROB *prirob = dynamic_cast<PendingReceiverIROB*>(pirob);
        if (!prirob->finish(expected_bytes)) {
            //throw CMMFatalError("Tried to end already-done IROB", hdr);
            dbgprintf("do_end_irob: already-finished IROB %d, ", id);
            if (prirob->is_complete()) {
                dbgprintf_plain("resending ACK\n");
            } else {
                dbgprintf_plain("still waiting for deps and/or data\n");
                if (!resend_request) {
                    resend_request = true;
                    if (prirob->placeholder) {
                        req_type = resend_request_type_t(req_type |
                                                         CMM_RESEND_REQUEST_DEPS);
                    }
                    if (prirob->recvd_bytes != expected_bytes) {
                        req_type = resend_request_type_t(req_type | 
                                                         CMM_RESEND_REQUEST_DATA);
                    }
                }
            }
        }

        if (resend_request) {
            IROBSchedulingData data(id, req_type);
            csock->irob_indexes.resend_requests.insert(data);
        }

        sk->incoming_irobs.release_if_ready(prirob, ReadyIROB());

        if (prirob->is_complete()) {
            csock->irob_indexes.waiting_acks.insert(IROBSchedulingData(id, false));
            pthread_cond_broadcast(&sk->scheduling_state_cv);
        }
    }

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
    irob_id_t id = ntohl(hdr.op.irob_chunk.id);
    int datalen = ntohl(hdr.op.irob_chunk.datalen);
    char *buf = read_data_buffer(id, datalen, hdr);

    hdr.op.irob_chunk.data = buf;

    {
        PthreadScopedLock lock(&sk->scheduling_state_lock);
        
        PendingIROB *pirob = sk->incoming_irobs.find(id);
        if (!pirob) {
            if (sk->incoming_irobs.past_irob_exists(id)) {
                throw CMMFatalError("Tried to add to committed IROB", hdr);
            } else {
                //throw CMMFatalError("Tried to add to nonexistent IROB", hdr);
                dbgprintf("Receiver got IROB_chunk for IROB %d; "
                          "creating placeholder\n", id);
                pirob = sk->incoming_irobs.make_placeholder(id);
                bool ret = sk->incoming_irobs.insert(pirob, false);
                assert(ret); // since it was absent before now

                IROBSchedulingData data(id, CMM_RESEND_REQUEST_DEPS);
                csock->irob_indexes.resend_requests.insert(data);
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

        sk->incoming_irobs.release_if_ready(prirob, ReadyIROB());

        if (prirob->is_complete()) {
            csock->irob_indexes.waiting_acks.insert(IROBSchedulingData(id, false));
            pthread_cond_broadcast(&sk->scheduling_state_cv);
        }
    }
    
    TIME(end);
    TIMEDIFF(begin, end, diff);
    dbgprintf("Added chunk %d in IROB %d, took %lu.%06lu seconds\n",
	      ntohl(hdr.op.irob_chunk.seqno), id, diff.tv_sec, diff.tv_usec);
}

#if 0
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

        csock->irob_indexes.waiting_acks.insert(IROBSchedulingData(id, false));
        pthread_cond_broadcast(&sk->scheduling_state_cv);
    }

    TIME(end);
    TIMEDIFF(begin, end, diff);
    dbgprintf("Received default IROB %d, took %lu.%06lu seconds\n",
	      id, diff.tv_sec, diff.tv_usec);
}
#endif

void
CSocketReceiver::do_new_interface(struct CMMSocketControlHdr hdr)
{
    assert(ntohs(hdr.type) == CMM_CONTROL_MSG_NEW_INTERFACE);
    struct net_interface iface = hdr.op.new_interface;
    iface.labels = ntohl(hdr.op.new_interface.labels);
    iface.bandwidth = ntohl(hdr.op.new_interface.bandwidth);
    iface.RTT = ntohl(hdr.op.new_interface.RTT);

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
        
        /* wake up any selectors */
        dbgprintf("Received goodbye for msocket %d; waking selectors\n",
                  sk->sock);
        shutdown(sk->select_pipe[1], SHUT_RDWR);
    }
    /* Note: the senders will still wait for all waiting ACKs
     * to be sent before finalizing the shutdown. */
}

void
CSocketReceiver::do_request_resend(struct CMMSocketControlHdr hdr)
{
    assert(ntohs(hdr.type) == CMM_CONTROL_MSG_RESEND_REQUEST);
    irob_id_t id = ntohl(hdr.op.resend_request.id);
    resend_request_type_t request = (resend_request_type_t)ntohl(hdr.op.resend_request.request);
    ssize_t offset = ntohl(hdr.op.resend_request.offset);

    sk->resend_request_received(id, request, offset);
}
