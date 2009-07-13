#include "cmm_socket_sender.h"
#include "libcmm_irob.h"
#include "pending_irob.h"
#include "pending_sender_irob.h"
#include "debug.h"
#include "cmm_socket_control.h"
#include "common.h"
#include "csocket_mapping.h"
#include "csocket.h"
#include "thunks.h"

CMMSocketSender::CMMSocketSender(CMMSocketImpl *sk_)
  : sk(sk_)
{
    handle(CMM_CONTROL_MSG_BEGIN_IROB, this,
           &CMMSocketSender::pass_to_worker_by_labels);
    handle(CMM_CONTROL_MSG_END_IROB, this, 
           &CMMSocketSender::pass_to_any_worker_prefer_labels);
    handle(CMM_CONTROL_MSG_IROB_CHUNK, this,
           &CMMSocketSender::pass_to_worker_by_labels);
    handle(CMM_CONTROL_MSG_NEW_INTERFACE, this, 
           &CMMSocketSender::pass_to_any_worker);
    handle(CMM_CONTROL_MSG_DOWN_INTERFACE, this, 
           &CMMSocketSender::pass_to_any_worker);
    handle(CMM_CONTROL_MSG_ACK, this, 
           &CMMSocketSender::pass_to_any_worker_prefer_labels);
}

CMMSocketSender::~CMMSocketSender()
{
    PendingIROBHash::accessor ac;
    while (pending_irobs.any(ac)) {
        PendingIROB *victim = ac->second;
        pending_irobs.erase(ac);
        delete victim;
        ac.release();
    }
}

/* This function blocks until the data has been sent.
 * If the socket is non-blocking, we need to implement that here.
 */
int
CMMSocketSender::begin_irob(irob_id_t next_irob, 
                            int numdeps, const irob_id_t *deps,
                            u_long send_labels, u_long recv_labels,
                            resume_handler_t resume_handler, void *rh_arg)
{
    irob_id_t id = next_irob;

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
        PendingIROB *pirob = new PendingSenderIROB(req.hdr.op.begin_irob,
                                                   resume_handler, rh_arg);
        PendingIROBHash::accessor ac;
        bool success = pending_irobs.insert(ac, pirob);
        assert(success);
    }
    
    long rc = enqueue_and_wait_for_completion(req);
    if (rc < 0) {
        pending_irobs.erase(id);
    }
    
    return rc;
}

/* This function blocks until the data has been sent.
 * If the socket is non-blocking, we need to implement that here.
 */
int
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
            return -1;
        }
        pirob->finish();
    }
    
    struct CMMSocketRequest req;
    req.requester_tid = pthread_self();
    req.hdr.type = htons(CMM_CONTROL_MSG_END_IROB);
    req.hdr.op.end_irob.id = htonl(id);
    
    long rc = enqueue_and_wait_for_completion(req);
    if (rc != 0) {
        dbgprintf("end irob %d failed entirely; connection must be gone\n", id);
        return -1;
    }
    return rc;
}

/* This function blocks until the data has been sent.
 * If the socket is non-blocking, we need to implement that here.
 */
long
CMMSocketSender::irob_chunk(irob_id_t id, const void *buf, size_t len, 
                            int flags)
{
    struct irob_chunk_data chunk;
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

    long rc = enqueue_and_wait_for_completion(req);
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
CMMSocketSender::ack(irob_id_t id, u_long seqno)
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
    PendingSenderIROB *psirob = static_cast<PendingSenderIROB*>(pirob);
    assert(psirob);

    psirob->ack(seqno);
    if (psirob->is_acked()) {
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
    CSocket *csock = sk->csock_map.new_csock_with_labels(0, 0);
    if (!csock) {
        throw Exception::make("No connection available!", req);
    }
    csock->send(req);
}

void
CMMSocketSender::pass_to_any_worker_prefer_labels(struct CMMSocketRequest req)
{
    PendingIROBHash::const_accessor ac;
    irob_id_t id;
    struct CMMSocketControlHdr& hdr = req.hdr;
    if (req.hdr.type == htons(CMM_CONTROL_MSG_END_IROB)) {
        id = ntohl(hdr.op.end_irob.id);
    } else if (req.hdr.type == htons(CMM_CONTROL_MSG_ACK)) {
        id = ntohl(hdr.op.ack.id);
    } else assert(0);

    if (!pending_irobs.find(ac, id)) {
        dbgprintf("Sending message for non-existent IROB %d\n", id);
        throw CMMException();        
    }
    PendingIROB *pirob = ac->second;
    assert(pirob);
    CSocket *csock = sk->csock_map.new_csock_with_labels(pirob->send_labels,
                                                         pirob->recv_labels);
    ac.release();

    if (csock) {
        csock->send(req);
    } else {
        pass_to_any_worker(req);        
    }
}

struct BlockingRequest {
    CMMSocketSender *sendr;
    struct CMMSocketRequest req;

    BlockingRequest(CMMSocketSender *sendr_, struct CMMSocketRequest req_)
        : sendr(sendr_), req(req_) {}
};

static void resume_request(BlockingRequest *breq)
{
    breq->sendr->enqueue(breq->req);
    delete breq;
}

void
CMMSocketSender::pass_to_worker_by_labels(struct CMMSocketRequest req)
{
    PendingIROBHash::const_accessor ac;
    irob_id_t id;
    if (req.hdr.type == htons(CMM_CONTROL_MSG_BEGIN_IROB)) {
        id = ntohl(req.hdr.op.begin_irob.id);
    } else if (req.hdr.type == htons(CMM_CONTROL_MSG_IROB_CHUNK)) {
        id = ntohl(req.hdr.op.irob_chunk.id);
    } else assert(0);

    if (!pending_irobs.find(ac, id)) {
        dbgprintf("Sending message for non-existent IROB %d\n", id);
        throw CMMException();
    }
    PendingIROB *pirob = ac->second;
    assert(pirob);
    CSocket *csock = sk->csock_map.new_csock_with_labels(pirob->send_labels, 
                                                         pirob->recv_labels);
    if (csock) {
        csock->send(req);
    } else {
        PendingSenderIROB *psirob = static_cast<PendingSenderIROB*>(pirob);
        assert(psirob);
        if (psirob->resume_handler) {
            enqueue_handler(sk->sock, pirob->send_labels, pirob->recv_labels,
                            psirob->resume_handler, psirob->rh_arg);
            signal_completion(req.requester_tid, CMM_DEFERRED);
        } else {
            /* no resume handler, so just wait until a suitable network
             * becomes available and try the request again. */
            enqueue_handler(sk->sock, pirob->send_labels, pirob->recv_labels,
                            (resume_handler_t)resume_request, 
                            new BlockingRequest(this, req));
        }
    }
}

/* returns result of underlying send, or -1 on error */
long 
CMMSocketSender::enqueue_and_wait_for_completion(CMMSocketRequest req)
{
    long rc;
    pthread_t self = pthread_self();
    struct AppThread& thread = app_threads[self];

    pthread_mutex_lock(&thread.mutex);
    thread.rc = CMM_INVALID_RC;
    enqueue(req);
    while (thread.rc == CMM_INVALID_RC) {
        pthread_cond_wait(&thread.cv, &thread.mutex);
    }
    rc = thread.rc;
    pthread_mutex_unlock(&thread.mutex);

    return rc;
}

void 
CMMSocketSender::signal_completion(pthread_t requester_tid, long rc)
{
    assert(app_threads.find(requester_tid) != app_threads.end());
    struct AppThread& thread = app_threads[requester_tid];

    pthread_mutex_lock(&thread.mutex);
    thread.rc = rc;
    pthread_cond_signal(&thread.cv);
    pthread_mutex_unlock(&thread.mutex);
}
