#include "csocket_sender.h"
#include "csocket.h"
#include "libcmm_irob.h"
#include "cmm_socket.private.h"
#include "thunks.h"
#include "pending_irob.h"
#include "pending_sender_irob.h"
#include "csocket_mapping.h"
#include "pthread_util.h"
#include <signal.h>
#include <pthread.h>

CSocketSender::CSocketSender(CSocketPtr csock_) 
  : csock(csock_), sk(get_pointer(csock_->sk)) {}

void
CSocketSender::Run()
{
    char name[MAX_NAME_LEN+1];
    memset(name, 0, MAX_NAME_LEN+1);
    snprintf(name, MAX_NAME_LEN, "CSockSender %d", csock->osfd);
    set_thread_name(name);

    sigset_t sigset;
    sigemptyset(&sigset);
    sigaddset(&sigset, SIGPIPE); // ignore SIGPIPE
    pthread_sigmask(SIG_BLOCK, &sigset, NULL);

    PthreadScopedLock lock(&sk->scheduling_state_lock);
    try {
        while (1) {
            if (sk->is_shutting_down()) {
                if (csock->irob_indexes.waiting_acks.empty()
                    && sk->irob_indexes.waiting_acks.empty()
                    && sk->outgoing_irobs.empty()) {

                    {
                        PthreadScopedLock shdwn_lock(&sk->shutdown_mutex);
                        if (!sk->goodbye_sent && !sk->sending_goodbye) {
                            shdwn_lock.release();
                            sk->sending_goodbye = true;
                            goodbye();
                        }
                    }
                    lock.release();
                    PthreadScopedLock shdwn_lock(&sk->shutdown_mutex);
                    while (!sk->remote_shutdown) {
                        pthread_cond_wait(&sk->shutdown_cv,
                                          &sk->shutdown_mutex);
                    }
                    return;
                }
            }
            
            if (csock->csock_recvr == NULL) {
                dbgprintf("Hmm, the receiver died.  Checking why.\n");
                if (sk->is_shutting_down() && sk->goodbye_sent) {
                    throw std::runtime_error("Connection closed");
                } else {
                    struct CMMSocketControlHdr hdr;
                    hdr.type = htons(CMM_CONTROL_MSG_GOODBYE);
                    throw CMMControlException("Receiver died due to "
                                              "socket error; "
                                              "sender is quitting", hdr);
                }
            }

            if (schedule_on_my_labels()) {
                continue;
            }
            if (schedule_unlabeled()) {
                continue;
            }
            
            pthread_cond_wait(&sk->scheduling_state_cv,
                              &sk->scheduling_state_lock);
            // something happened; we might be able to do some work
        }
    } catch (CMMControlException& e) {
        //csock->remove();
        sk->csock_map->remove_csock(csock);
        CSocketPtr replacement = sk->csock_map->new_csock_with_labels(0);
        if (replacement) {
            // pass off any work I didn't get to finish; it will
            //  be passed to the correct thread or deferred
            //  as appropriate
            replacement->irob_indexes = csock->irob_indexes;
        } else {
            // this connection is hosed, so make sure everything
            // gets cleaned up as if we had done a graceful shutdown
            dbgprintf("Connection %d is hosed; forcefully shutting down\n",
                      sk->sock);
            shutdown(sk->select_pipe[1], SHUT_RDWR);

            PthreadScopedLock lock(&sk->shutdown_mutex);
            sk->shutting_down = true;
            sk->remote_shutdown = true;
            sk->goodbye_sent = true;
            pthread_cond_broadcast(&sk->shutdown_cv);
        }
        pthread_cond_broadcast(&sk->scheduling_state_cv);
        // csock will get cleaned up in Finish()
        throw;
    }
}

bool
CSocketSender::schedule_on_my_labels()
{
    return schedule_work(csock->irob_indexes);
}

bool
CSocketSender::schedule_unlabeled()
{
    return schedule_work(sk->irob_indexes);
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
        send_acks(data, indexes);
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

    CSocketPtr csock =
        op->sk->csock_map->new_csock_with_labels(psirob->send_labels);
    
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

    if (csock->matches(pirob->send_labels)) {
        return false;
    }

    CSocketPtr match = 
        sk->csock_map->new_csock_with_labels(pirob->send_labels);
    if (!match) {
        if (!pirob->complete) {
            if (psirob->resume_handler) {
                enqueue_handler(sk->sock,
                                pirob->send_labels, 
                                psirob->resume_handler, psirob->rh_arg);
                pirob->status = CMM_DEFERRED;
            } else {
                enqueue_handler(sk->sock,
                                pirob->send_labels, 
                                (resume_handler_t)resume_operation_thunk,
                                new ResumeOperation(sk, data));
                pirob->status = CMM_FAILED; // really "blocking"
            }
        } else {
            /* no way to tell the application that we failed to send
             * this IROB.  Just drop it, and let the application 
             * do the usual end-to-end check (e.g. response timeout). 
             */
            dbgprintf("Warning: silently dropping IROB %d after failing to send it.\n",
                      pirob->id);
            sk->outgoing_irobs.erase(pirob->id);
            delete pirob;
        }
        return true;
    } else {
        assert(match != csock); // since csock->matches returned false

        // pass this task to the right thread
        if (data.seqno == INVALID_IROB_SEQNO) {
            match->irob_indexes.new_irobs.insert(data);
        } else {
            match->irob_indexes.new_chunks.insert(data);
        }
        pthread_cond_broadcast(&sk->scheduling_state_cv);
        return true;
    }

    return false;
}

void 
CSocketSender::begin_irob(const IROBSchedulingData& data)
{
    irob_id_t id = data.id;

    PendingIROB *pirob = sk->outgoing_irobs.find(id);
    if (!pirob) {
        // shouldn't get here if it doesn't exist
        assert(0);
    }

    if (delegate_if_necessary(pirob, data)) {
        return;
    }
    // at this point, we know that this task is ours

    struct CMMSocketControlHdr hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.send_labels = htonl(pirob->send_labels);

    irob_id_t *deps = NULL;

    struct iovec vec[3];
    vec[0].iov_base = &hdr;
    vec[0].iov_len = sizeof(hdr);
    vec[1].iov_base = NULL;
    vec[1].iov_len = 0;
    vec[2].iov_base = NULL;
    vec[2].iov_len = 0;
    size_t count = 1;
    int numdeps = pirob->deps.size();
    if (numdeps > 0) {
        deps = new irob_id_t[numdeps];
        int i = 0;
        for (irob_id_set::iterator it = pirob->deps.begin();
             it != pirob->deps.end(); it++) {
            deps[i++] = htonl(*it);
        }
    }
    
    if (pirob->is_anonymous()) {
        hdr.type = htons(CMM_CONTROL_MSG_DEFAULT_IROB);
        hdr.op.default_irob.id = htonl(id);
        
        hdr.op.default_irob.numdeps = htonl(numdeps);
        if (numdeps > 0) {
            vec[count].iov_base = deps;
            vec[count].iov_len = numdeps * sizeof(irob_id_t);
            count++;
        }

        assert(pirob->chunks.size() == 1);
        struct irob_chunk_data chunk = pirob->chunks.front();
        hdr.op.default_irob.datalen = htonl(chunk.datalen);

        vec[count].iov_base = chunk.data;
        vec[count].iov_len = chunk.datalen;
        count++;
    } else {
        hdr.type = htons(CMM_CONTROL_MSG_BEGIN_IROB);
        hdr.op.begin_irob.id = htonl(id);
        hdr.op.begin_irob.numdeps = htonl(numdeps);

        vec[count].iov_base = deps;
        vec[count].iov_len = numdeps * sizeof(irob_id_t);
        count++;
    }

    size_t bytes = vec[0].iov_len + vec[1].iov_len + vec[2].iov_len;

    dbgprintf("About to send message: %s", hdr.describe().c_str());
    if (deps) {
        dbgprintf_plain(" deps [ ");
        for (int i = 0; i < numdeps; ++i) {
            dbgprintf_plain("%d ", ntohl(deps[i]));
        }
        dbgprintf_plain("]");
    }
    dbgprintf_plain("\n");
    pthread_mutex_unlock(&sk->scheduling_state_lock);
    int rc = writev(csock->osfd, vec, count);
    pthread_mutex_lock(&sk->scheduling_state_lock);

    delete [] deps;

    if (rc != (ssize_t)bytes) {
        sk->irob_indexes.new_irobs.insert(data);
        pthread_cond_broadcast(&sk->scheduling_state_cv);
        perror("CSocketSender: writev");
        throw CMMControlException("Socket error", hdr);
    }

    sk->remove_if_unneeded(pirob);
}

void 
CSocketSender::end_irob(const IROBSchedulingData& data)
{
    PendingIROB *pirob = sk->outgoing_irobs.find(data.id);
    if (!pirob) {
        // shouldn't get here if it doesn't exist
        assert(0);
    }

    struct CMMSocketControlHdr hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.type = htons(CMM_CONTROL_MSG_END_IROB);
    hdr.op.end_irob.id = htonl(data.id);
    hdr.op.end_irob.num_chunks = htonl(pirob->chunks.size());
    hdr.send_labels = htonl(csock->local_iface.labels);

    dbgprintf("About to send message: %s\n", hdr.describe().c_str());
    pthread_mutex_unlock(&sk->scheduling_state_lock);
    int rc = write(csock->osfd, &hdr, sizeof(hdr));
    pthread_mutex_lock(&sk->scheduling_state_lock);

    if (rc != sizeof(hdr)) {
        sk->irob_indexes.finished_irobs.insert(data);
        pthread_cond_broadcast(&sk->scheduling_state_cv);
        perror("CSocketSender: write");
        throw CMMControlException("Socket error", hdr);
    }
    
    // Don't do this!  
    //   1) It's a race to call this without
    //      holding the scheduling_state_lock.
    //   2) This will be called later when the 
    //      ACK arrives.  Even if called
    //      with the lock here, it would never
    //      remove the PendingIROB.
    //sk->remove_if_unneeded(pirob);
}

void 
CSocketSender::irob_chunk(const IROBSchedulingData& data)
{
    irob_id_t id = data.id;

    PendingIROB *pirob = sk->outgoing_irobs.find(id);
    if (!pirob) {
        // shouldn't get here if it doesn't exist
        assert(0);
    }

    if (delegate_if_necessary(pirob, data)) {
        return;
    }
    // at this point, we know that this task is ours

    struct CMMSocketControlHdr hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.type = htons(CMM_CONTROL_MSG_IROB_CHUNK);
    hdr.send_labels = htonl(pirob->send_labels);

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

    dbgprintf("About to send message: %s\n", hdr.describe().c_str());
    pthread_mutex_unlock(&sk->scheduling_state_lock);
    int rc = writev(csock->osfd, vec, 2);
    pthread_mutex_lock(&sk->scheduling_state_lock);
    if (rc != (ssize_t)(sizeof(hdr) + chunk.datalen)) {
        sk->irob_indexes.new_chunks.insert(data);
        pthread_cond_broadcast(&sk->scheduling_state_cv);
        perror("CSocketSender: writev");
        throw CMMControlException("Socket error", hdr);
    }

    sk->remove_if_unneeded(pirob);
}

void 
CSocketSender::new_interface(struct net_interface iface)
{
    struct CMMSocketControlHdr hdr;
    hdr.type = htons(CMM_CONTROL_MSG_NEW_INTERFACE);
    hdr.op.new_interface.ip_addr = iface.ip_addr;
    hdr.op.new_interface.labels = htonl(iface.labels);

    hdr.send_labels = htonl(0);

    dbgprintf("About to send message: %s\n", hdr.describe().c_str());
    pthread_mutex_unlock(&sk->scheduling_state_lock);
    int rc = write(csock->osfd, &hdr, sizeof(hdr));
    pthread_mutex_lock(&sk->scheduling_state_lock);

    if (rc != sizeof(hdr)) {
        sk->changed_local_ifaces.insert(iface);
        pthread_cond_broadcast(&sk->scheduling_state_cv);
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

    hdr.send_labels = htonl(0);

    dbgprintf("About to send message: %s\n", hdr.describe().c_str());
    pthread_mutex_unlock(&sk->scheduling_state_lock);
    int rc = write(csock->osfd, &hdr, sizeof(hdr));
    pthread_mutex_lock(&sk->scheduling_state_lock);

    if (rc != sizeof(hdr)) {
        sk->changed_local_ifaces.insert(iface);
        pthread_cond_broadcast(&sk->scheduling_state_cv);
        perror("CSocketSender: write");
        throw CMMControlException("Socket error", hdr);        
    }
}

void 
CSocketSender::send_acks(const IROBSchedulingData& data, 
                         IROBSchedulingIndexes& indexes)
{
    const size_t MAX_ACKS = 150;
    struct CMMSocketControlHdr hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.type = htons(CMM_CONTROL_MSG_ACK);
    hdr.send_labels = htonl(csock->local_iface.labels);
    hdr.op.ack.id = htonl(data.id);
    
    irob_id_t acked_irobs[MAX_ACKS];
    memset(acked_irobs, 0, MAX_ACKS * sizeof(irob_id_t));
    
    size_t ack_count = 0;
    std::set<IROBSchedulingData>::iterator head = indexes.waiting_acks.begin();
    std::set<IROBSchedulingData>::iterator it = head;
    while (ack_count < MAX_ACKS && it != indexes.waiting_acks.end()) {
        acked_irobs[ack_count] = htonl(it->id);

        ++it;
        ++ack_count;
    }

    std::vector<IROBSchedulingData> tmp(head, it);
    indexes.waiting_acks.erase(head, it);

    hdr.op.ack.num_acks = htonl(ack_count);

    int numvecs = 1;
    struct iovec vec[2];
    vec[0].iov_base = &hdr;
    vec[0].iov_len = sizeof(hdr);
    vec[1].iov_base = NULL;
    vec[1].iov_len = 0;
    if (ack_count > 0) {
        numvecs = 2;
        vec[1].iov_base = acked_irobs;
        vec[1].iov_len = ack_count * sizeof(irob_id_t);
    }
    
    size_t datalen = vec[0].iov_len + vec[1].iov_len;
    dbgprintf("About to send %d ACKs\n", ack_count + 1);

    pthread_mutex_unlock(&sk->scheduling_state_lock);
    int rc = writev(csock->osfd, vec, numvecs);
    pthread_mutex_lock(&sk->scheduling_state_lock);

    if (rc != (int)datalen) {
        // re-insert all ACKs
        sk->irob_indexes.waiting_acks.insert(tmp.begin(), tmp.end());

        pthread_cond_broadcast(&sk->scheduling_state_cv);
        perror("CSocketSender: write");
        throw CMMControlException("Socket error", hdr);
    }
}

void 
CSocketSender::goodbye()
{
    struct CMMSocketControlHdr hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.type = htons(CMM_CONTROL_MSG_GOODBYE);
    hdr.send_labels = htonl(csock->local_iface.labels);

    dbgprintf("About to send message: %s\n", hdr.describe().c_str());
    pthread_mutex_unlock(&sk->scheduling_state_lock);
    int rc = write(csock->osfd, &hdr, sizeof(hdr));
    pthread_mutex_lock(&sk->scheduling_state_lock);

    if (rc != sizeof(hdr)) {
        sk->sending_goodbye = false;
        pthread_cond_broadcast(&sk->scheduling_state_cv);
        perror("CSocketSender: write");
        throw CMMControlException("Socket error", hdr);
    }
    
    PthreadScopedLock lock(&sk->shutdown_mutex);
    sk->goodbye_sent = true;

    pthread_cond_broadcast(&sk->shutdown_cv);
}


void
CSocketSender::Finish(void)
{
    {
        PthreadScopedLock lock(&sk->scheduling_state_lock);
        shutdown(csock->osfd, SHUT_WR);
        sk->csock_map->remove_csock(csock);
        csock->csock_sendr = NULL;
        //pthread_cond_broadcast(&sk->scheduling_state_cv);
    }
    dbgprintf("Exiting.\n");

    // nobody will pthread_join to the sender now, so detach
    //  to make sure the memory gets reclaimed
    detach();

    delete this; // the last thing that will ever be done with this
}
