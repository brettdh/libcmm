#include "csocket_sender.h"
#include "csocket.h"
#include "libcmm_irob.h"
#include "cmm_socket.private.h"
#include "thunks.h"
#include "pending_irob.h"
#include "pending_sender_irob.h"
#include "irob_scheduling.h"
#include "csocket_mapping.h"
#include "pthread_util.h"
#include <signal.h>
#include <pthread.h>
#include "timeops.h"
#include <sys/ioctl.h>
#include <linux/sockios.h>
#include <vector>
using std::vector;

CSocketSender::CSocketSender(CSocketPtr csock_) 
  : csock(csock_), sk(get_pointer(csock_->sk)) 
{
    trickle_timeout.tv_sec = -1;
    trickle_timeout.tv_nsec = 0;
}

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

            if (schedule_work(csock->irob_indexes)) {
                continue;
            }
            if (schedule_work(sk->irob_indexes)) {
                continue;
            }
            
            if (trickle_timeout.tv_sec >= 0) {
                dbgprintf("Waiting until %lu.%09lu to check again for trickling\n",
                          trickle_timeout.tv_sec, trickle_timeout.tv_nsec);
                int rc = pthread_cond_timedwait(&sk->scheduling_state_cv,
                                                &sk->scheduling_state_lock,
                                                &trickle_timeout);
                trickle_timeout.tv_sec = -1;
                if (rc != 0) {
                    dbgprintf("pthread_cond_timedwait failed, rc=%d\n", rc);
                }
            } else {
                pthread_cond_wait(&sk->scheduling_state_cv,
                                  &sk->scheduling_state_lock);
            }
            // something happened; we might be able to do some work
        }
    } catch (CMMControlException& e) {
        pthread_mutex_unlock(&sk->scheduling_state_lock);
        sk->csock_map->remove_csock(csock);
        CSocketPtr replacement = sk->csock_map->new_csock_with_labels(0);
        pthread_mutex_lock(&sk->scheduling_state_lock);
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

bool CSocketSender::schedule_work(IROBSchedulingIndexes& indexes)
{
    bool did_something = false;

    IROBSchedulingData data;

    if (indexes.new_irobs.pop(data)) {
        if (!begin_irob(data)) {
            indexes.new_irobs.insert(data);
        } else {
            did_something = true;
        }
    }
    
    if (indexes.new_chunks.pop(data)) {
        if (!irob_chunk(data)) {
            indexes.new_irobs.insert(data);
        } else {
            did_something = true;
        }
    }
    
    if (indexes.finished_irobs.pop(data)) {
        end_irob(data);
        did_something = true;
    }
    
    if (indexes.waiting_acks.pop(data)) {
        send_acks(data, indexes);
        did_something = true;
    }

    struct net_interface iface;

    if (pop_item(sk->changed_local_ifaces, iface)) {
        new_interface(iface);
        did_something = true;
    }

    if (pop_item(sk->down_local_ifaces, iface)) {
        down_interface(iface);
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
    u_long send_labels = 0;
    {
        PthreadScopedLock lock(&op->sk->scheduling_state_lock);
        PendingIROB *pirob = op->sk->outgoing_irobs.find(op->data.id);
        assert(pirob);
        PendingSenderIROB *psirob = dynamic_cast<PendingSenderIROB*>(pirob);
        assert(psirob);
        send_labels = psirob->send_labels;
    }

    CSocketPtr csock =
        op->sk->csock_map->new_csock_with_labels(send_labels);
    
    PthreadScopedLock lock(&op->sk->scheduling_state_lock);
    IROBSchedulingIndexes& indexes = (csock 
                                      ? csock->irob_indexes 
                                      : op->sk->irob_indexes);
    if (op->data.chunks_ready) {
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

    u_long send_labels = pirob->send_labels;

    pthread_mutex_unlock(&sk->scheduling_state_lock);

    if (csock->matches(send_labels)) {
        return false;
    }
    
    CSocketPtr match = 
        sk->csock_map->new_csock_with_labels(send_labels);

    if (!match) {
        if (send_labels & CMM_LABEL_BACKGROUND) {
            // No actual background network, so let's trickle 
            // on the available network
            return false;
        }
    }

    pthread_mutex_lock(&sk->scheduling_state_lock);
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
        if (!data.chunks_ready) {
            match->irob_indexes.new_irobs.insert(data);
        } else {
            match->irob_indexes.new_chunks.insert(data);
        }
        pthread_cond_broadcast(&sk->scheduling_state_cv);
        return true;
    }

    return false;
}

bool CSocketSender::okay_to_send_bg(struct timeval& time_since_last_fg)
{
    struct timeval now;
    TIME(now);
    dbgprintf("Checking whether to trickle background data...\n");
    if (!sk->okay_to_send_bg(now, time_since_last_fg)) {
        dbgprintf("     ...too soon after last FG transmission\n");
        return false;
    }
    
    int unsent_bytes = 0;
    int rc = ioctl(csock->osfd, SIOCOUTQ, &unsent_bytes);
    if (rc < 0) {
        dbgprintf("     ...failed to check socket send buffer: %s\n",
                  strerror(errno));
        return false;
    } else if (unsent_bytes > 0) {
        dbgprintf("     ...socket buffer not empty\n");
        return false;
    }
    dbgprintf("     ...yes.\n");
    return true;
}

/* returns true if I actually sent it; false if not */
bool
CSocketSender::begin_irob(const IROBSchedulingData& data)
{
    irob_id_t id = data.id;

    PendingIROB *pirob = sk->outgoing_irobs.find(id);
    if (!pirob) {
        // shouldn't get here if it doesn't exist
        assert(0);
    }

    if (delegate_if_necessary(pirob, data)) {
        // we passed it off, so make sure that we don't
        // try to re-insert data into the IROBSchedulingIndexes.
        return true;
    }
    // at this point, we know that this task is ours

    if (data.send_labels & CMM_LABEL_BACKGROUND &&
        !csock->matches(data.send_labels)) {
        struct timeval dummy;
        if (!okay_to_send_bg(dummy)) {
            struct timespec rel_timeout = {
                CMMSocketImpl::bg_wait_time.tv_sec,
                CMMSocketImpl::bg_wait_time.tv_usec*1000
            };
            trickle_timeout = abs_time(rel_timeout);
            return false;
        }
        // after this point, we've committed to sending the
        // BG BEGIN_IROB message on the FG socket.
    }

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
    
    hdr.type = htons(CMM_CONTROL_MSG_BEGIN_IROB);
    hdr.op.begin_irob.id = htonl(id);
    hdr.op.begin_irob.numdeps = htonl(numdeps);
    
    vec[count].iov_base = deps;
    vec[count].iov_len = numdeps * sizeof(irob_id_t);
    count++;
    

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

    PendingSenderIROB *psirob = dynamic_cast<PendingSenderIROB*>(pirob);
    assert(psirob);
    psirob->announced = true;
    if (pirob->is_anonymous()) {
        csock->irob_indexes.new_chunks.insert(IROBSchedulingData(pirob->id, 1));
        csock->irob_indexes.finished_irobs.insert(data);
    }
    // WRONG WRONG WRONG WRONG WRONG.  only remove after ACK.
    //sk->remove_if_unneeded(pirob);

    if (data.send_labels & CMM_LABEL_ONDEMAND) {
        TIME(sk->last_fg);
    }
    return true;
}

struct SumFunctor {
    size_t sum;
    SumFunctor() : sum(0) {}
    void operator()(const struct irob_chunk_data& chunk) {
        sum += chunk.datalen;
    }
};

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
    hdr.op.end_irob.expected_bytes = htonl(for_each(pirob->chunks.begin(),
                                                    pirob->chunks.end(),
                                                    SumFunctor()).sum);
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

/* returns true if I actually sent it; false if not */
bool
CSocketSender::irob_chunk(const IROBSchedulingData& data)
{
    irob_id_t id = data.id;

    PendingIROB *pirob = sk->outgoing_irobs.find(id);
    if (!pirob) {
        /* must've been ACK'd already */
        return true;
    }
    PendingSenderIROB *psirob = dynamic_cast<PendingSenderIROB*>(pirob);
    assert(psirob);

    if (delegate_if_necessary(pirob, data)) {
        return true;
    }
    // at this point, we know that this task is ours

    // default to sending next chunk (maybe tweak to avoid extra roundtrips?)
    ssize_t chunksize = 0;

    if (data.send_labels & CMM_LABEL_BACKGROUND &&
        !csock->matches(data.send_labels)) {
        struct timeval time_since_last_fg;
        if (!okay_to_send_bg(time_since_last_fg)) {
            struct timespec rel_timeout = {
                CMMSocketImpl::bg_wait_time.tv_sec,
                CMMSocketImpl::bg_wait_time.tv_usec*1000
            };

            trickle_timeout = abs_time(rel_timeout);
            return false;
        }
        
        chunksize = CMMSocketImpl::trickle_chunksize(time_since_last_fg);

        // after this point, we've committed to sending a
        // BG chunk (chunksize bytes) on the FG socket.
    }

    struct CMMSocketControlHdr hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.type = htons(CMM_CONTROL_MSG_IROB_CHUNK);
    hdr.send_labels = htonl(pirob->send_labels);

    u_long seqno = 0;

    vector<struct iovec> irob_vecs = psirob->get_ready_bytes(chunksize, seqno);
    if (chunksize == 0) {
        return true;
    }

    hdr.op.irob_chunk.id = htonl(id);
    hdr.op.irob_chunk.seqno = htonl(seqno);
    hdr.op.irob_chunk.datalen = htonl(chunksize);
    hdr.op.irob_chunk.data = NULL;

    struct iovec *vec = new struct iovec[1 + irob_vecs.size()];
    vec[0].iov_base = &hdr;
    vec[0].iov_len = sizeof(hdr);
    for (size_t i = 0; i < irob_vecs.size(); ++i) {
        vec[i+1] = irob_vecs[i];
    }
    //vec[1].iov_base = chunk.data;
    //vec[1].iov_len = chunk.datalen;

    dbgprintf("About to send message: %s\n", hdr.describe().c_str());
    pthread_mutex_unlock(&sk->scheduling_state_lock);
    int rc = writev(csock->osfd, vec, irob_vecs.size() + 1);
    delete [] vec;
    pthread_mutex_lock(&sk->scheduling_state_lock);
    if (rc != (ssize_t)(sizeof(hdr) + chunksize)) {
        sk->irob_indexes.new_chunks.insert(data);
        pthread_cond_broadcast(&sk->scheduling_state_cv);
        perror("CSocketSender: writev");
        throw CMMControlException("Socket error", hdr);
    }

    // It might've been ACK'd and removed, so check first
    psirob = dynamic_cast<PendingSenderIROB*>(sk->outgoing_irobs.find(id));
    if (psirob) {
        psirob->mark_sent(chunksize);

        if (psirob->is_complete() && !psirob->end_announced) {
            psirob->end_announced = true;
            if (psirob->send_labels == 0) {
                sk->irob_indexes.finished_irobs.insert(IROBSchedulingData(id));
            } else {
                csock->irob_indexes.finished_irobs.insert(IROBSchedulingData(id));
            }
        } 

        // more chunks to send, potentially, so make sure someone sends them.
        csock->irob_indexes.new_chunks.insert(data);
    }

    // WRONG WRONG WRONG WRONG.  only remove after ACK.
    //sk->remove_if_unneeded(pirob);

    if (data.send_labels & CMM_LABEL_ONDEMAND) {
        TIME(sk->last_fg);
    }
    return true;
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
        sk->irob_indexes.waiting_acks.insert_range(tmp.begin(), tmp.end());

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
