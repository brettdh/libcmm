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
    PendingIROBHash::accessor ac;
    bool success = pending_irobs.insert(ac, id);
    assert(success);
    ac->second = new PendingIROB(req.hdr.op.begin_irob);

    enqueue(req);
    int rc = wait_for_completion();
    if (rc < 0) {
        
    }
    return id;
}

/* This function blocks until the data has been sent.
 * If the socket is non-blocking, we need to implement that here.
 */
int
CMMSocketSender::end_irob(irob_id_t id)
{
    PendingIROBHash::const_accessor ac;
    if (!pending_irobs.find(ac, id)) {
        return -1;
    }
    /* TODO: left off here. */
    
    enqueue(req);
    wait_for_completion();
    return 0;
}

/* This function blocks until the data has been sent.
 * If the socket is non-blocking, we need to implement that here.
 */
int
CMMSocketSender::irob_chunk(irob_id_t, const void *buf, size_t len, int flags)
{
}

void 
CMMSocketSender::new_interface(struct in_addr ip_addr, u_long labels)
{
}

void 
CMMSocketSender::down_interface(struct in_addr ip_addr)
{
}

void 
CMMSocketSender::ack(irob_id_t id, u_long seqno = INVALID_IROB_SEQNO)
{
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
