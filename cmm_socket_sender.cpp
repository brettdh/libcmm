#include "cmm_socket_sender.h"
#include "irob.h"
#include "pending_irob.h"

CMMSocketSender::CMMSocketSender(CMMSocketImpl *sk_)
    : sk(sk_)
{
    handle(CMM_CONTROL_MSG_BEGIN_IROB, &CMMSocketSender::pass_to_worker_by_labels);
    handle(CMM_CONTROL_MSG_END_IROB, &CMMSocketSender::pass_to_worker_by_labels);
    handle(CMM_CONTROL_MSG_IROB_CHUNK, &CMMSocketSender::pass_to_worker_by_labels);
    handle(CMM_CONTROL_MSG_NEW_INTERFACE, 
           &CMMSocketSender::pass_to_any_worker);
    handle(CMM_CONTROL_MSG_DOWN_INTERFACE, 
           &CMMSocketSender::pass_to_any_worker);
    handle(CMM_CONTROL_MSG_ACK, &CMMSocketSender::pass_to_worker_by_labels);
}

/* This function blocks until the data has been sent.
 * If the socket is non-blocking, we need to implement that here.
 */
irob_id_t 
CMMSocketSender::begin_irob(u_long send_labels, u_long recv_labels,
                            int numdeps, irob_id_t *deps)
{
    irob_id_t id = sk->next_irob++;

    struct CMMSocketRequest req;
    req.requester_tid = pthread_self();
    req.hdr.type = htons(CMM_CONTROL_MSG_BEGIN_IROB);
    req.hdr.op.begin_irob.id = htonl(id);
    req.hdr.op.begin_irob.send_labels = htonl(send_labels);
    req.hdr.op.begin_irob.recv_labels = htonl(recv_labels);
    req.hdr.op.begin_irob.numdeps = htonl(numdeps);
    req.hdr.op.begin_irob.deps = NULL;
    if (numdeps > 0) {
        req.hdr.op.begin_irob.deps = new irob_id_t[numdeps];
        for (int i = 0; i < numdeps; i++) {
            req.hdr.op.begin_irob.deps[i] = htonl(deps[i]);
        }
    }

    {
        PendingIROBHash::accessor ac;
        bool success = pending_irobs.insert(ac, id);
        assert(success);
        ac->second = new PendingIROB(req.hdr.op.begin_irob);
    }
    
    enqueue_and_wait_for_completion(req);
    
    return id;
}

/* This function blocks until the data has been sent.
 * If the socket is non-blocking, we need to implement that here.
 */
void
CMMSocketSender::end_irob(irob_id_t id)
{
    {
        PendingIROBHash::accessor ac;
        if (!pending_irobs.find(ac, id)) {
            return -1;
        }
        
        PendingIROB *pirob = ac->second;
        assert(pirob);
        if (pirob->is_complete()) {
            dbgprintf("Trying to complete IROB %lu, "
                      "which is already complete\n", id);
            throw CMMException();
        }
        pirob->finish();
    }
    
    struct CMMSocketRequest req;
    req.requester_tid = pthread_self();
    req.hdr.type = htons(CMM_CONTROL_MSG_END_IROB);
    req.hdr.op.end_irob.id = htonl(id);
    
    enqueue_and_wait_for_completion(req);
}

/* This function blocks until the data has been sent.
 * If the socket is non-blocking, we need to implement that here.
 */
ssize_t
CMMSocketSender::irob_chunk(irob_id_t id, const void *buf, size_t len, int flags)
{
    struct irob_chunk chunk;
    {
        PendingIROBHash::accessor ac;
        if (!pending_irobs.find(ac, id)) {
            dbgprintf("Tried to add to nonexistent IROB %d\n", id);
            throw CMMException();
        }
        
        PendingIROB *pirob = ac->second;
        assert(pirob);
        if (pirob->is_complete()) {
            dbgprintf("Tried to add to complete IROB %d\n", id);
            throw CMMException();
        }
        chunk.id = id;
        chunk.seqno = INVALID_IROB_SEQNO; /* will be overwritten 
                                           * with valid seqno */
        chunk.datalen = len;
        chunk.data = new char[len];
        memcpy(chunk.data, buf, len);
        pirob->add_chunk(chunk); /* writes correct seqno into struct */
    }

    struct CMMSocketRequest req;
    req.requester_tid = pthread_self();
    req.hdr.type = htons(CMM_CONTROL_MSG_IROB_CHUNK);

    chunk.id = htonl(chunk.id);
    chunk.seqno = htonl(chunk.seqno);
    chunk.datalen = htonl(chunk.datalen);
    req.hdr.op.irob_chunk = chunk;

    ssize_t rc = enqueue_and_wait_for_completion(req);
    return rc;
}

void 
CMMSocketSender::new_interface(struct in_addr ip_addr, u_long labels)
{
    struct CMMSocketRequest req;
    req.requester_tid = 0; /* signifying that we won't wait for the result */
    req.hdr.type = CMM_CONTROL_MSG_NEW_INTERFACE;
    req.hdr.op.new_interface.ip_addr = ip_addr;
    req.hdr.op.new_interface.labels = htonl(labels);

    enqueue(req);
}

void 
CMMSocketSender::down_interface(struct in_addr ip_addr)
{
    struct CMMSocketRequest req;
    req.requester_tid = 0; /* signifying that we won't wait for the result */
    req.hdr.type = CMM_CONTROL_MSG_DOWN_INTERFACE;
    req.hdr.op.down_interface.ip_addr = ip_addr;

    enqueue(req);
}

void 
CMMSocketSender::ack(irob_id_t id, u_long seqno = INVALID_IROB_SEQNO)
{
    struct CMMSocketRequest req;
    req.requester_tid = 0;
    req.hdr.type = CMM_CONTROL_MSG_ACK;
    req.hdr.op.ack.id = htonl(id);
    req.hdr.op.ack.seqno = htonl(seqno);

    enqueue(req);
}

void
CMMSocketSender::ack_received(irob_id_t id, u_long seqno)
{
    PendingIROBHash::accessor ac;
    if (!pending_irobs.find(ac, id)) {
        dbgprintf("Ack received for non-existent IROB %d\n", id);
        throw CMMException();
    }
    PendingIROB *pirob = ac->second;
    assert(pirob);
    pirob->ack(seqno);
    if (pirob->is_acked()) {
        pending_irobs.erase(ac);
        delete pirob;
    }
}


/* These functions pass the header strucure to worker threads, 
 * who in turn send the data on their socket.
 * The worker threads assume integers in the headers are
 * already in network byte order, so the above functions should
 * ensure that this is true. */
void 
CMMSocketSender::pass_to_any_worker(struct CMMSocketRequest req)
{
    CSocket *csock = csocks.new_csock_with_labels(0, 0);
    if (!csock) {
        throw CMMControlException("No connection available!", req);
    }
    csock->csock_sendr->enqueue(req);
}

void
CMMSocketSender::pass_to_worker_by_labels(struct CMMSocketRequest req)
{
    /* If this action fails at the worker, it will be re-enqueued.
     * We need a way to have it fall back on another worker if the
     * labeled one fails.
     *
     * Since the "is a suitable network available?" question has been 
     * answered well before this point, the only failure scenario here
     * is that in which the connection fails after the network availability
     * has been checked but before the packet gets to the socket.
     * 
     * Maybe the request structure needs a flag that tells whether
     * this operation has failed once before.
     */
}

CSocket *
CMMSocketSender::preapprove(irob_id_t id)
{
    
}
