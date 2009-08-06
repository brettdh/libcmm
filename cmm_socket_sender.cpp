#include "cmm_socket_sender.h"
#include "cmm_socket_receiver.h"
#include "libcmm_irob.h"
#include "pending_irob.h"
#include "pending_sender_irob.h"
#include "pending_receiver_irob.h"
#include "debug.h"
#include "cmm_socket_control.h"
#include "common.h"
#include "csocket_mapping.h"
#include "csocket.h"
#include "thunks.h"
#include "timeops.h"

CMMSocketSender::CMMSocketSender(CMMSocketImpl *sk_)
    : sk(sk_), shutting_down(false), remote_shutdown(false)
{
    if (pthread_mutex_init(&shutdown_mutex, NULL) != 0 ||
	pthread_cond_init(&shutdown_cv, NULL) != 0) {
	assert(0);
    }

    handle(CMM_CONTROL_MSG_BEGIN_IROB, this,
           &CMMSocketSender::pass_to_worker_by_labels);
    handle(CMM_CONTROL_MSG_END_IROB, this, 
           &CMMSocketSender::pass_to_any_worker_prefer_labels);
    handle(CMM_CONTROL_MSG_IROB_CHUNK, this,
           &CMMSocketSender::pass_to_worker_by_labels);
    handle(CMM_CONTROL_MSG_DEFAULT_IROB, this,
           &CMMSocketSender::pass_to_worker_by_labels);
    handle(CMM_CONTROL_MSG_NEW_INTERFACE, this, 
           &CMMSocketSender::pass_to_any_worker);
    handle(CMM_CONTROL_MSG_DOWN_INTERFACE, this, 
           &CMMSocketSender::pass_to_any_worker);
    handle(CMM_CONTROL_MSG_ACK, this, 
           &CMMSocketSender::pass_to_any_worker_prefer_labels);
    handle(CMM_CONTROL_MSG_GOODBYE, this, 
           &CMMSocketSender::pass_to_any_worker);
}

CMMSocketSender::~CMMSocketSender()
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

bool
CMMSocketSender::is_shutting_down()
{
    pthread_mutex_lock(&shutdown_mutex);
    bool shdwn = shutting_down;
    pthread_mutex_unlock(&shutdown_mutex);
    return shdwn;
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
    if (is_shutting_down()) {
	dbgprintf("Tried to begin IROB, but mc_socket %d is shutting down\n", 
		  sk->sock);
	errno = EPIPE;
	return CMM_FAILED;
    }

    struct timeval begin, end, diff;
    TIME(begin);

    irob_id_t id = next_irob;

    struct CMMSocketRequest req;
    req.requester_tid = pthread_self();
    req.hdr.type = htons(CMM_CONTROL_MSG_BEGIN_IROB);
    req.hdr.op.begin_irob.id = htonl(id);
    req.hdr.op.begin_irob.numdeps = htonl(numdeps);
    req.hdr.op.begin_irob.deps = NULL;
    req.hdr.send_labels = htonl(send_labels);
    req.hdr.recv_labels = htonl(recv_labels);

    if (numdeps > 0) {
        req.hdr.op.begin_irob.deps = new irob_id_t[numdeps];
        for (int i = 0; i < numdeps; i++) {
            req.hdr.op.begin_irob.deps[i] = htonl(deps[i]);
        }
    }
    
    {
        PendingIROB *pirob = new PendingSenderIROB(req.hdr.op.begin_irob, 
						   send_labels, recv_labels,
                                                   resume_handler, rh_arg);
        PendingIROBHash::accessor ac;
        bool success = pending_irobs.insert(ac, pirob);
        assert(success);
    }
    
    long rc = enqueue_and_wait_for_completion(req);
    if (rc < 0) {
        pending_irobs.erase(id);
    }
    TIME(end);
    TIMEDIFF(begin, end, diff);
    dbgprintf("Completed request in %lu.%06lu seconds (%s)\n",
	      diff.tv_sec, diff.tv_usec,
	      req.describe().c_str());
    
    return rc;
}

/* This function blocks until the data has been sent.
 * If the socket is non-blocking, we need to implement that here.
 */
int
CMMSocketSender::end_irob(irob_id_t id)
{
    if (is_shutting_down()) {
	dbgprintf("Tried to end IROB, but mc_socket %d is shutting down\n", 
		  sk->sock);
	errno = EPIPE;
	return CMM_FAILED;
    }

    struct timeval begin, end, diff;
    TIME(begin);

    u_long send_labels, recv_labels;
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

	send_labels = pirob->send_labels;
	recv_labels = pirob->recv_labels;
    }
    
    struct CMMSocketRequest req;
    req.requester_tid = pthread_self();
    req.hdr.type = htons(CMM_CONTROL_MSG_END_IROB);
    req.hdr.op.end_irob.id = htonl(id);
    req.hdr.send_labels = htonl(send_labels);
    req.hdr.recv_labels = htonl(recv_labels);
    
    long rc = enqueue_and_wait_for_completion(req);
    if (rc != 0) {
        dbgprintf("end irob %d failed entirely; connection must be gone\n", id);
        return -1;
    } else {
	PendingIROBHash::accessor ac;
	if (pending_irobs.find(ac, id)) {
	    remove_if_unneeded(ac);
	}
    }
    TIME(end);
    TIMEDIFF(begin, end, diff);
    dbgprintf("Completed request in %lu.%06lu seconds (%s)\n",
	      diff.tv_sec, diff.tv_usec,
	      req.describe().c_str());

    return rc;
}


/* This function blocks until the data has been sent.
 * If the socket is non-blocking, we need to implement that here.
 */
long
CMMSocketSender::irob_chunk(irob_id_t id, const void *buf, size_t len, 
                            int flags)
{
    if (is_shutting_down()) {
	dbgprintf("Tried to send IROB chunk, but mc_socket %d is shutting down\n", 
		  sk->sock);
	errno = EPIPE;
	return CMM_FAILED;
    }

    struct timeval begin, end, diff;
    TIME(begin);

    struct irob_chunk_data chunk;
    u_long send_labels, recv_labels;
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
	PendingSenderIROB *psirob = dynamic_cast<PendingSenderIROB*>(pirob);
	assert(psirob);
	psirob->add_chunk(chunk); /* writes correct seqno into struct */
	
	send_labels = pirob->send_labels;
	recv_labels = pirob->recv_labels;
    }
    
    struct CMMSocketRequest req;
    req.requester_tid = pthread_self();
    req.hdr.type = htons(CMM_CONTROL_MSG_IROB_CHUNK);
    req.hdr.send_labels = htonl(send_labels);
    req.hdr.recv_labels = htonl(recv_labels);

    chunk.id = htonl(chunk.id);
    chunk.seqno = htonl(chunk.seqno);
    chunk.datalen = htonl(chunk.datalen);
    req.hdr.op.irob_chunk = chunk;

    long rc = enqueue_and_wait_for_completion(req);
    TIME(end);
    TIMEDIFF(begin, end, diff);
    dbgprintf("Completed request in %lu.%06lu seconds (%s)\n",
	      diff.tv_sec, diff.tv_usec,
	      req.describe().c_str());

    return rc;
}

int
CMMSocketSender::default_irob(irob_id_t next_irob, 
			      const void *buf, size_t len, int flags,
			      u_long send_labels, u_long recv_labels,
			      resume_handler_t resume_handler, void *rh_arg)
{
    if (is_shutting_down()) {
	dbgprintf("Tried to send default IROB, but mc_socket %d is shutting down\n", 
		  sk->sock);
	errno = EPIPE;
	return CMM_FAILED;
    }

    struct timeval begin, end, diff;
    TIME(begin);

    irob_id_t id = next_irob;

    struct default_irob_data default_irob;
    default_irob.id = htonl(id);
    default_irob.datalen = htonl(len);
    default_irob.data = new char[len];
    memcpy(default_irob.data, buf, len);
    {
        PendingIROB *pirob = new PendingSenderIROB(default_irob, 
						   send_labels, recv_labels,
                                                   resume_handler, rh_arg);
        PendingIROBHash::accessor ac;
        bool success = pending_irobs.insert(ac, pirob);
        assert(success);
    }
    
    struct CMMSocketRequest req;
    req.requester_tid = pthread_self();
    req.hdr.type = htons(CMM_CONTROL_MSG_DEFAULT_IROB);
    req.hdr.op.default_irob = default_irob;
    req.hdr.send_labels = htonl(send_labels);
    req.hdr.recv_labels = htonl(recv_labels);

    long rc = enqueue_and_wait_for_completion(req);
    TIME(end);
    TIMEDIFF(begin, end, diff);
    dbgprintf("Completed request in %lu.%06lu seconds (%s)\n",
	      diff.tv_sec, diff.tv_usec,
	      req.describe().c_str());

    return rc;
}

void 
CMMSocketSender::new_interface(struct in_addr ip_addr, u_long labels)
{
    if (is_shutting_down()) {
	return;
    }

    struct CMMSocketRequest req;
    req.requester_tid = 0; /* signifying that we won't wait for the result */
    req.hdr.type = htons(CMM_CONTROL_MSG_NEW_INTERFACE);
    req.hdr.op.new_interface.ip_addr = ip_addr;
    req.hdr.op.new_interface.labels = htonl(labels);

    req.hdr.send_labels = req.hdr.recv_labels = htonl(0);

    enqueue(req);
}

void 
CMMSocketSender::down_interface(struct in_addr ip_addr)
{
    if (is_shutting_down()) {
	return;
    }

    struct CMMSocketRequest req;
    req.requester_tid = 0; /* signifying that we won't wait for the result */
    req.hdr.type = htons(CMM_CONTROL_MSG_DOWN_INTERFACE);
    req.hdr.op.down_interface.ip_addr = ip_addr;

    req.hdr.send_labels = req.hdr.recv_labels = htonl(0);

    enqueue(req);
}

void 
CMMSocketSender::ack(irob_id_t id, u_long seqno, 
		     u_long ack_send_labels, u_long ack_recv_labels)
{
    struct CMMSocketRequest req;
    req.requester_tid = 0;
    req.hdr.type = htons(CMM_CONTROL_MSG_ACK);
    req.hdr.op.ack.id = htonl(id);
    req.hdr.op.ack.seqno = htonl(seqno);
    req.hdr.send_labels = htonl(ack_send_labels);
    req.hdr.recv_labels = htonl(ack_recv_labels);

    struct timeval begin, end, diff;
    TIME(begin);
    enqueue(req);
    TIME(end);
    TIMEDIFF(begin, end, diff);
    dbgprintf("Enqueued request in %lu.%06lu seconds (%s)\n",
	      diff.tv_sec, diff.tv_usec,
	      req.describe().c_str());
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
    PendingSenderIROB *psirob = dynamic_cast<PendingSenderIROB*>(pirob);
    assert(psirob);

    psirob->ack(seqno);
    remove_if_unneeded(ac);
}

/* call only with ac held */
void CMMSocketSender::remove_if_unneeded(PendingIROBHash::accessor& ac)
{
    PendingIROB *pirob = ac->second;
    assert(pirob);
    PendingSenderIROB *psirob = dynamic_cast<PendingSenderIROB*>(pirob);
    assert(psirob);
    if (psirob->is_acked() && psirob->is_complete()) {
        pending_irobs.erase(ac);
        delete pirob;
	ac.release();

	if (pending_irobs.empty()) {
	    pthread_mutex_lock(&shutdown_mutex);
	    if (shutting_down) {
		pthread_cond_signal(&shutdown_cv);
	    }
	    pthread_mutex_unlock(&shutdown_mutex);
	}
    }
}

void
CMMSocketSender::goodbye(bool remote_initiated)
{
    struct CMMSocketRequest req;
    if (remote_initiated) {
	req.requester_tid = pthread_self();
    } else {
	req.requester_tid = 0;
    }
    req.hdr.type = htons(CMM_CONTROL_MSG_GOODBYE);
    req.hdr.send_labels = req.hdr.recv_labels = htonl(0);

    if (is_shutting_down()) {
	return;
    }

    pthread_mutex_lock(&shutdown_mutex);
    shutting_down = true;
    if (remote_initiated) {
	remote_shutdown = true;
    }
    while (!pending_irobs.empty()) {
	pthread_cond_wait(&shutdown_cv, &shutdown_mutex);
    }
    if (remote_initiated) {
	enqueue_and_wait_for_completion(req);
    } else {
	enqueue(req);
    }
    while (!remote_shutdown) {
	pthread_cond_wait(&shutdown_cv, &shutdown_mutex);
    }
    pthread_mutex_unlock(&shutdown_mutex);
    sk->recvr->shutdown();
}

void 
CMMSocketSender::goodbye_acked(void)
{
    pthread_mutex_lock(&shutdown_mutex);
    assert(shutting_down);
    remote_shutdown = true;
    pthread_cond_signal(&shutdown_cv);
    pthread_mutex_unlock(&shutdown_mutex);
}


/* These functions pass the header strucure to worker threads, 
 * who in turn send the data on their socket.
 * The worker threads assume integers in the headers are
 * already in network byte order, so the above functions should
 * ensure that this is true. */
void 
CMMSocketSender::pass_to_any_worker(struct CMMSocketRequest req)
{
    CSocket *csock = NULL;
    try {
        csock = sk->csock_map->new_csock_with_labels(0, 0);
    } catch (std::runtime_error& e) {
        dbgprintf("Error passing message to worker: %s\n", e.what());
        signal_completion(req.requester_tid, CMM_FAILED);
        return;
    }

    if (!csock) {
        throw Exception::make("No connection available!", req);
    }
    csock->send(req);
}

void
CMMSocketSender::pass_to_any_worker_prefer_labels(struct CMMSocketRequest req)
{
    u_long send_labels = ntohl(req.hdr.send_labels);
    u_long recv_labels = ntohl(req.hdr.recv_labels);
    short type = ntohs(req.hdr.type);
    assert(type == CMM_CONTROL_MSG_END_IROB ||
	   type == CMM_CONTROL_MSG_ACK);

    CSocket *csock = NULL;
    try {
        csock = sk->csock_map->new_csock_with_labels(send_labels,
						     recv_labels);
    } catch (std::runtime_error& e) {
	/* this means we ran out of memory/FDs. */
        dbgprintf("Error passing message to worker pref lbls: %s\n", e.what());
	pass_to_any_worker(req);
        return;
    }

    if (csock) {
        csock->send(req);
    } else {
        pass_to_any_worker(req);        
    }
}

/* TODO: what about a timeout for these? Maybe just check if
 * SO_TIMEOUT is set for the socket. */
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
    irob_id_t id;
    u_long send_labels = ntohl(req.hdr.send_labels);
    u_long recv_labels = ntohl(req.hdr.recv_labels);
    if (req.hdr.type == htons(CMM_CONTROL_MSG_BEGIN_IROB)) {
        id = ntohl(req.hdr.op.begin_irob.id);
    } else if (req.hdr.type == htons(CMM_CONTROL_MSG_IROB_CHUNK)) {
        id = ntohl(req.hdr.op.irob_chunk.id);
    } else if (req.hdr.type == htons(CMM_CONTROL_MSG_DEFAULT_IROB)) {
	id = ntohl(req.hdr.op.default_irob.id);
    } else assert(0);

    CSocket *csock = NULL;
    try {
        csock = sk->csock_map->new_csock_with_labels(send_labels, 
						     recv_labels);
    } catch (std::runtime_error& e) {
        dbgprintf("Error passing message to worker by labels: %s\n", e.what());
        signal_completion(req.requester_tid, CMM_FAILED);
        return;
    }

    if (csock) {
        csock->send(req);
    } else {
	PendingIROBHash::const_accessor ac;
	if (!pending_irobs.find(ac, id)) {
	    throw Exception::make("Sending message for non-existent IROB\n", req);
	}
	PendingIROB *pirob = ac->second;
	assert(pirob);
	PendingSenderIROB *psirob = dynamic_cast<PendingSenderIROB*>(pirob);
        assert(psirob);
        if (psirob->resume_handler) {
            enqueue_handler(sk->sock, send_labels, recv_labels,
                            psirob->resume_handler, psirob->rh_arg);
            signal_completion(req.requester_tid, CMM_DEFERRED);
        } else {
            /* no resume handler, so just wait until a suitable network
             * becomes available and try the request again. */
	    /* TODO: nonblocking */
            enqueue_handler(sk->sock, send_labels, recv_labels,
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
    if (requester_tid != 0) {
	assert(app_threads.find(requester_tid) != app_threads.end());
	struct AppThread& thread = app_threads[requester_tid];
	
	pthread_mutex_lock(&thread.mutex);
	thread.rc = rc;
	pthread_cond_signal(&thread.cv);
	pthread_mutex_unlock(&thread.mutex);
    }
}
