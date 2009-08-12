#include "csocket_sender.h"
#include "csocket.h"
#include "libcmm_irob.h"
#include "cmm_socket.private.h"
#include "thunks.h"
#include "pending_irob.h"
#include "pending_sender_irob.h"

CSocketSender::CSocketSender(CSocket *csock_) : csock(csock_) {}

void
CSocketSender::Run()
{
    pthread_mutex_lock(&csock->sk->scheduling_state_lock);
    while (1) {
        if (csock->sk->is_shutting_down()) {
            if (csock->sk->outgoing_irobs.empty()) {
                if (!csock->sk->goodbye_sent) {
                    goodbye();
                }
            }
        }
        
        if (schedule_on_my_labels()) {
            continue;
        }
        if (schedule_unlabeled()) {
            continue;
        }

        pthread_cond_wait(&csock->sk->scheduling_state_cv,
                          &csock->sk->scheduling_state_lock);
        // something happened; we might be able to do some work
    }
}

template <typename ItemType, typename ContainerType>
static bool pop_item(ContainerType& container, ItemType& item)
{
    if (container.empty()) {
        return false;
    }
    
    item = *container.begin();
    container.erase(container.begin());
    return true;
}

bool
CSocketSender::schedule_on_my_labels()
{
    return schedule_work(csock->irob_indexes);
}

bool
CSocketSender::schedule_unlabeled()
{
    return schedule_work(csock->sk->irob_indexes);
}

bool CSocketSender::schedule_work(IROBSchedulingIndexes& indexes)
{
    bool did_something = false;

    IROBSchedulingData data;

    if (pop_item(indexes.new_irobs, data)) {
        begin_irob(data);
        did_something = true;
    }
    
    if (pop_item(indexes.new_chunks, data)) {
        irob_chunk(data);
        did_something = true;
    }
    
    if (pop_item(indexes.finished_irobs, data)) {
        end_irob(data);
        did_something = true;
    }
    
    if (pop_item(indexes.waiting_acks, data)) {
        ack(data);
        did_something = true;
    }

    return did_something;    
}

struct ResumeOperation {
    CMMSocketImpl *sk;
    IROBSchedulingData data;

    ResumeOperation(CMMSocketImpl *sk_, IROBSchedulingData data_) 
        : sk(sk_), data(data_) {}
};

void resume_operation_thunk(ResumeOperation *op)
{
    assert(op);
    PthreadScopedLock lock(&op->sk->scheduling_state_lock);
    PendingIROB *pirob = op->sk->outgoing_irobs.find(op->data.id);
    assert(pirob);
    PendingSenderIROB *psirob = dynamic_cast<PendingSenderIROB*>(pirob);
    assert(psirob);

    CSocket *csock =
        op->sk->csock_map->new_csock_with_labels(psirob->send_labels,
                                                 psirob->recv_labels);
    
    IROBSchedulingIndexes& indexes = (csock 
                                      ? csock->irob_indexes 
                                      : op->sk->irob_indexes);
    if (op->data.seqno > INVALID_IROB_SEQNO) {
        indexes.new_chunks.insert(op->data);
    } else {
        indexes.new_irobs.insert(op->data);
    }
    pthread_cond_broadcast(&op->sk->scheduling_state_cv);
    delete op;
}

bool
CSocketSender::delegate_if_necessary(PendingIROB *pirob, const IROBSchedulingData& data)
{
    assert(pirob);
    PendingSenderIROB *psirob = dynamic_cast<PendingSenderIROB*>(pirob);
    assert(psirob);

    if (csock->matches(pirob->send_labels, pirob->recv_labels)) {
        return false;
    }

    CSocket *match = 
        csock->sk->csock_map->new_csock_with_labels(pirob->send_labels,
                                                    pirob->recv_labels);
    if (!match) {
        if (psirob->resume_handler) {
            enqueue_handler(csock->sk->sock,
                            pirob->send_labels, pirob->recv_labels, 
                            psirob->resume_handler, psirob->rh_arg);
            pthread_t waiting_thread = pirob->waiting_thread;
            pirob->waiting_thread = (pthread_t)0;
            csock->sk->signal_completion(waiting_thread, CMM_DEFERRED);
        } else {
            enqueue_handler(csock->sk->sock,
                            pirob->send_labels, pirob->recv_labels,
                            (resume_handler_t)resume_operation_thunk,
                            new ResumeOperation(csock->sk, data));
        }
        return true;
    } else {
        assert(match != csock); // since csock->matches returned false

        // pass this task to the right thread
        match->irob_indexes.new_irobs.insert(data);
        pthread_cond_broadcast(&csock->sk->scheduling_state_cv);
        return true;
    }

    return false;
}

void 
CSocketSender::begin_irob(const IROBSchedulingData& data)
{
    irob_id_t id = data.id;

    PendingIROB *pirob = csock->sk->outgoing_irobs.find(id);
    if (!pirob) {
        // shouldn't get here if it doesn't exist
        assert(0);
    }

    if (delegate_if_necessary(pirob, data)) {
        return;
    }
    // at this point, we know that this task is ours

    struct CMMSocketControlHdr hdr;
    hdr.send_labels = htonl(pirob->send_labels);
    hdr.recv_labels = htonl(pirob->recv_labels);

    irob_id_t *deps = NULL;

    struct iovec vec[2];
    vec[0].iov_base = &hdr;
    vec[0].iov_len = sizeof(hdr);
    vec[1].iov_base = NULL;
    vec[1].iov_len = 0;

    if (pirob->is_anonymous()) {
        hdr.type = htons(CMM_CONTROL_MSG_DEFAULT_IROB);
        hdr.op.default_irob.id = htonl(id);
        hdr.op.default_irob.data = NULL;
        
        assert(pirob->chunks.size() == 1);
        struct irob_chunk_data chunk = pirob->chunks.front();
        hdr.op.default_irob.datalen = htonl(chunk.datalen);

        vec[1].iov_base = chunk.data;
        vec[1].iov_len = chunk.datalen;
    } else {
        int numdeps = pirob->deps.size();
        
        hdr.type = htons(CMM_CONTROL_MSG_BEGIN_IROB);
        hdr.op.begin_irob.id = htonl(id);
        hdr.op.begin_irob.numdeps = htonl(numdeps);
        hdr.op.begin_irob.deps = NULL;
        
        if (numdeps > 0) {
            deps = new irob_id_t[numdeps];
            int i = 0;
            for (PendingIROB::irob_id_set::iterator it = pirob->deps.begin();
                 it != pirob->deps.end(); it++) {
                deps[i++] = htonl(*it);
            }
        }

        vec[1].iov_base = deps;
        vec[1].iov_len = sizeof(irob_id_t) * numdeps;
    }

    size_t bytes = vec[0].iov_len + vec[1].iov_len;
    size_t count = vec[1].iov_base ? 2 : 1;

    pthread_mutex_unlock(&csock->sk->scheduling_state_lock);
    int rc = writev(csock->osfd, vec, count);
    pthread_mutex_lock(&csock->sk->scheduling_state_lock);

    delete [] deps;

    if (rc != (ssize_t)bytes) {
        csock->sk->irob_indexes.new_irobs.insert(data);
        pthread_cond_broadcast(&csock->sk->scheduling_state_cv);
        perror("CSocketSender: writev");
        throw CMMControlException("Socket error", hdr);
    }

    pthread_t waiting_thread = pirob->waiting_thread;
    pirob->waiting_thread = (pthread_t)0;

    if (pirob->is_anonymous()) {
        csock->sk->signal_completion(waiting_thread, vec[1].iov_len);
    } else {
        csock->sk->signal_completion(waiting_thread, 0);
    }
}

void 
CSocketSender::end_irob(const IROBSchedulingData& data)
{
    PendingIROB *pirob = csock->sk->outgoing_irobs.find(data.id);
    if (!pirob) {
        // shouldn't get here if it doesn't exist
        assert(0);
    }

    struct CMMSocketControlHdr hdr;
    hdr.type = htons(CMM_CONTROL_MSG_END_IROB);
    hdr.op.end_irob.id = htonl(data.id);
    hdr.send_labels = htonl(csock->local_iface.labels);
    hdr.recv_labels = htonl(csock->remote_iface.labels);

    pthread_mutex_unlock(&csock->sk->scheduling_state_lock);
    int rc = write(csock->osfd, &hdr, sizeof(hdr));
    pthread_mutex_lock(&csock->sk->scheduling_state_lock);

    if (rc != sizeof(hdr)) {
        csock->sk->irob_indexes.finished_irobs.insert(data);
        pthread_cond_broadcast(&csock->sk->scheduling_state_cv);
        perror("CSocketSender: write");
        throw CMMControlException("Socket error", hdr);
    }
    
    pthread_t waiting_thread = pirob->waiting_thread;
    pirob->waiting_thread = (pthread_t)0;
    csock->sk->signal_completion(waiting_thread, 0);
}

void 
CSocketSender::irob_chunk(const IROBSchedulingData& data)
{
    irob_id_t id = data.id;

    PendingIROB *pirob = csock->sk->outgoing_irobs.find(id);
    if (!pirob) {
        // shouldn't get here if it doesn't exist
        assert(0);
    }

    if (delegate_if_necessary(pirob, data)) {
        return;
    }
    // at this point, we know that this task is ours

    struct CMMSocketControlHdr hdr;
    hdr.type = htons(CMM_CONTROL_MSG_IROB_CHUNK);
    hdr.send_labels = htonl(pirob->send_labels);
    hdr.recv_labels = htonl(pirob->recv_labels);

    // chunks start at seqno==1; chunk N is at pirob->chunks[N-1]
    assert(pirob->chunks.size() >= data.seqno);
    struct irob_chunk_data chunk = pirob->chunks[data.seqno - 1];
    assert(chunk.data);

    hdr.op.irob_chunk.id = htonl(id);
    hdr.op.irob_chunk.seqno = htonl(data.seqno);
    hdr.op.irob_chunk.datalen = htonl(chunk.datalen);
    hdr.op.irob_chunk.data = NULL;

    struct iovec vec[2];
    vec[0].iov_base = &hdr;
    vec[0].iov_len = sizeof(hdr);
    vec[1].iov_base = chunk.data;
    vec[1].iov_len = chunk.datalen;

    pthread_mutex_unlock(&csock->sk->scheduling_state_lock);
    int rc = writev(csock->osfd, vec, 2);
    pthread_mutex_lock(&csock->sk->scheduling_state_lock);
    if (rc != (ssize_t)(sizeof(hdr) + chunk.datalen)) {
        csock->sk->irob_indexes.new_chunks.insert(data);
        pthread_cond_broadcast(&csock->sk->scheduling_state_cv);
        perror("CSocketSender: writev");
        throw CMMControlException("Socket error", hdr);
    }

    PendingSenderIROB *psirob = dynamic_cast<PendingSenderIROB*>(pirob);
    assert(psirob);
    pthread_t waiting_thread = psirob->waiting_threads[data.seqno];
    psirob->waiting_threads.erase(data.seqno);
    csock->sk->signal_completion(waiting_thread, chunk.datalen);
}

void 
CSocketSender::new_interface(struct net_interface iface)
{
    struct CMMSocketControlHdr hdr;
    hdr.type = htons(CMM_CONTROL_MSG_NEW_INTERFACE);
    hdr.op.new_interface.ip_addr = iface.ip_addr;
    hdr.op.new_interface.labels = htonl(iface.labels);

    hdr.send_labels = hdr.recv_labels = htonl(0);

    pthread_mutex_unlock(&csock->sk->scheduling_state_lock);
    int rc = write(csock->osfd, &hdr, sizeof(hdr));
    pthread_mutex_lock(&csock->sk->scheduling_state_lock);

    if (rc != sizeof(hdr)) {
        csock->sk->changed_local_ifaces.insert(iface);
        pthread_cond_broadcast(&csock->sk->scheduling_state_cv);
        perror("CSocketSender: write");
        throw CMMControlException("Socket error", hdr);        
    }
}

void 
CSocketSender::down_interface(struct net_interface iface)
{
    struct CMMSocketControlHdr hdr;
    hdr.type = htons(CMM_CONTROL_MSG_DOWN_INTERFACE);
    hdr.op.down_interface.ip_addr = iface.ip_addr;

    hdr.send_labels = hdr.recv_labels = htonl(0);

    pthread_mutex_unlock(&csock->sk->scheduling_state_lock);
    int rc = write(csock->osfd, &hdr, sizeof(hdr));
    pthread_mutex_lock(&csock->sk->scheduling_state_lock);

    if (rc != sizeof(hdr)) {
        csock->sk->changed_local_ifaces.insert(iface);
        pthread_cond_broadcast(&csock->sk->scheduling_state_cv);
        perror("CSocketSender: write");
        throw CMMControlException("Socket error", hdr);        
    }
}

void 
CSocketSender::ack(const IROBSchedulingData& data)
{
    struct CMMSocketControlHdr hdr;
    hdr.type = htons(CMM_CONTROL_MSG_ACK);
    hdr.op.ack.id = htonl(data.id);
    hdr.op.ack.seqno = htonl(data.seqno);
    hdr.send_labels = htonl(csock->local_iface.labels);
    hdr.recv_labels = htonl(csock->remote_iface.labels);

    pthread_mutex_unlock(&csock->sk->scheduling_state_lock);
    int rc = write(csock->osfd, &hdr, sizeof(hdr));
    pthread_mutex_lock(&csock->sk->scheduling_state_lock);

    if (rc != sizeof(hdr)) {
        csock->sk->irob_indexes.waiting_acks.insert(data);
        pthread_cond_broadcast(&csock->sk->scheduling_state_cv);
        perror("CSocketSender: write");
        throw CMMControlException("Socket error", hdr);
    }
}

void 
CSocketSender::goodbye()
{
    struct CMMSocketControlHdr hdr;
    hdr.type = htons(CMM_CONTROL_MSG_GOODBYE);
    hdr.send_labels = htonl(csock->local_iface.labels);
    hdr.recv_labels = htonl(csock->remote_iface.labels);
    
    csock->sk->goodbye_sent = true;
    pthread_mutex_unlock(&csock->sk->scheduling_state_lock);
    int rc = write(csock->osfd, &hdr, sizeof(hdr));
    pthread_mutex_lock(&csock->sk->scheduling_state_lock);

    if (rc != sizeof(hdr)) {
        csock->sk->goodbye_sent = false;
        pthread_cond_broadcast(&csock->sk->scheduling_state_cv);
        perror("CSocketSender: write");
        throw CMMControlException("Socket error", hdr);
    }
}


void
CSocketSender::Finish(void)
{
    csock->remove();
}
