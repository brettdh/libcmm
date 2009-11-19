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

#include "cmm_timing.h"

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
                    hdr.send_labels = 0;
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

            // resend End_IROB messages for all unACK'd IROBs whose
            //   ack timeouts have expired.
            // this will cause the receiver to send ACKs or
            //   Resend_Requests for each of them.
            vector<irob_id_t> unacked_irobs = sk->ack_timeouts.remove_expired();
            for (size_t i = 0; i < unacked_irobs.size(); ++i) {
                if (sk->outgoing_irobs.find(unacked_irobs[i]) != NULL) {
                    dbgprintf("ACK timeout expired for IROB %ld, resending End_IROB\n",
                              unacked_irobs[i]);
                    IROBSchedulingData refinished_irob(unacked_irobs[i], false);
                    sk->irob_indexes.finished_irobs.insert(refinished_irob);
                }
            }
            
            struct timespec timeout = {-1, 0};
            struct timespec first_ack_timeout;
            if (sk->ack_timeouts.get_earliest(first_ack_timeout)) {
                timeout = first_ack_timeout;
            }
            if (trickle_timeout.tv_sec > 0) {
                if (timeout.tv_sec > 0) {
                    timeout = (timercmp(&timeout, &trickle_timeout, <)
                               ? timeout : trickle_timeout);
                } else {
                    timeout = trickle_timeout;
                }
            }

            if (timeout.tv_sec > 0) {
                dbgprintf("Waiting until %lu.%09lu to check again for ",
                          timeout.tv_sec, timeout.tv_nsec);
                dbgprintf_plain(timercmp(&timeout, &trickle_timeout, ==)
                                ? "trickling\n" : "ACKs\n");
                int rc = pthread_cond_timedwait(&sk->scheduling_state_cv,
                                                &sk->scheduling_state_lock,
                                                &timeout);
                if (timercmp(&timeout, &trickle_timeout, ==)) {
                    trickle_timeout.tv_sec = -1;
                }
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
            replacement->irob_indexes.add(csock->irob_indexes);
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
        irob_id_t id = data.id;
        if (!begin_irob(data)) {
            indexes.new_irobs.insert(data);
        } else {
            did_something = true;
            while (indexes.new_chunks.remove(id, data)) {
                if (!irob_chunk(data)) {
                    indexes.new_chunks.insert(data);
                    break;
                }
            }
            if (indexes.finished_irobs.remove(id, data)) {
                end_irob(data);
            }
        }
    }
    
    if (indexes.new_chunks.pop(data)) {
        irob_id_t id = data.id;
        if (!irob_chunk(data)) {
            indexes.new_chunks.insert(data);
        } else {
            did_something = true;
            while (indexes.new_chunks.remove(id, data)) {
                if (!irob_chunk(data)) {
                    indexes.new_chunks.insert(data);
                    break;
                }
            }
            if (indexes.finished_irobs.remove(id, data)) {
                end_irob(data);
            }
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

    if (indexes.resend_requests.pop(data)) {
        resend_request(data);
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

/* Must hold sk->scheduling_state_lock when calling.
 * It's possible that pirob will be ACK'd, removed, and deleted
 * during the time when this function doesn't hold the lock.
 * Therefore, it checks this after re-acquiring the lock
 * and returns immediately if pirob has been ACK'd.  
 * It also updates the pointer passed in, just in case
 * 
 */
bool
CSocketSender::delegate_if_necessary(irob_id_t id, PendingIROB *& pirob, 
                                     const IROBSchedulingData& data)
{
    pirob = sk->outgoing_irobs.find(id);
    if (!pirob) {
        // already ACK'd; don't send anything for it
        return true;
    }
    assert(pirob);
    PendingSenderIROB *psirob = dynamic_cast<PendingSenderIROB*>(pirob);
    assert(psirob);

    u_long send_labels = pirob->send_labels;

    pthread_mutex_unlock(&sk->scheduling_state_lock);

    if (csock->matches(send_labels)) {
        pthread_mutex_lock(&sk->scheduling_state_lock);
        return false;
    }
    
    CSocketPtr match = 
        sk->csock_map->new_csock_with_labels(send_labels);

    pthread_mutex_lock(&sk->scheduling_state_lock);

    pirob = sk->outgoing_irobs.find(id);
    if (!pirob) {
        // just kidding; the ACK arrived while I wasn't holding the lock
        // no need to send this message after all
        return true;        
    }
    psirob = dynamic_cast<PendingSenderIROB*>(pirob);
    assert(psirob);

    if (!match) {
        if (send_labels & CMM_LABEL_BACKGROUND) {
            // No actual background network, so let's trickle 
            // on the available network
            return false;
        }
    }

    if (!match) {
        if (!pirob->complete) {
            // mc_end_irob hasn't returned yet for this IROB;
            //  we can still tell the application it failed
            //  or otherwise block/thunk
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
            dbgprintf("Warning: silently dropping IROB %ld after failing to send it.\n",
                      pirob->id);
            sk->outgoing_irobs.erase(pirob->id);
            delete pirob;
            pirob = NULL;
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

bool CSocketSender::okay_to_send_bg(ssize_t& chunksize)
{
    bool do_trickle = true;

    //dbgprintf("Checking whether to trickle background data...\n");

    struct timeval rel_timeout;
    
    // Removing the wait duration, in hope that the small chunksize
    //  is sufficient to limit interference with FG traffic
    if (false) {//!sk->okay_to_send_bg(time_since_last_fg)) {
        /*
        dbgprintf("     ...too soon after last FG transmission\n");
        do_trickle = false;
        struct timeval wait_time = sk->bg_wait_time();
        timersub(&wait_time, &time_since_last_fg, 
                 &rel_timeout);
        */
    } else {
	chunksize = csock->trickle_chunksize();
	
	// hack to increase trickling rate when FG traffic ceases
	struct timeval time_since_last_fg, now;
	TIME(now);
	TIMEDIFF(sk->last_fg, now, time_since_last_fg);
	if (time_since_last_fg.tv_sec > 5) {
	    chunksize = csock->bandwidth();
	    do_trickle = true;
	}

        int unsent_bytes = 0;
        int rc = ioctl(csock->osfd, SIOCOUTQ, &unsent_bytes);
        if (rc < 0) {
            //dbgprintf("     ...failed to check socket send buffer: %s\n",
            //strerror(errno));
            do_trickle = false;
            //rel_timeout = sk->bg_wait_time();
        } else if (unsent_bytes >= chunksize) {
            /*dbgprintf("     ...socket buffer has %d bytes left; more than %d\n",
              unsent_bytes, csock->trickle_chunksize());*/
            do_trickle = false;

            /*
            u_long clear_time = (u_long)((unsent_bytes * (1000000.0 / csock->bandwidth()))
                                         + (csock->RTT() * 1000.0));
            rel_timeout.tv_sec = clear_time / 1000000;
            rel_timeout.tv_usec = clear_time - (rel_timeout.tv_sec * 1000000);
            */
        } else {
            chunksize = chunksize - unsent_bytes;
        }
    }

    if (do_trickle) {
        //dbgprintf("     ...yes.\n");
    } else {
        rel_timeout.tv_sec = 0;
        rel_timeout.tv_usec = 10000;

        /*dbgprintf("     ...waiting %ld.%06ld to check again\n",
          rel_timeout.tv_sec, rel_timeout.tv_usec);*/
        struct timespec rel_timeout_ts = {
            rel_timeout.tv_sec,
            rel_timeout.tv_usec*1000
        };
        trickle_timeout = abs_time(rel_timeout_ts);
    }
    return do_trickle;
}

/* returns true if I actually sent it; false if not */
bool
CSocketSender::begin_irob(const IROBSchedulingData& data)
{
    if (data.send_labels & CMM_LABEL_BACKGROUND) {
        pthread_mutex_unlock(&sk->scheduling_state_lock);
        bool fg_sock = csock->matches(CMM_LABEL_ONDEMAND);
        pthread_mutex_lock(&sk->scheduling_state_lock);
        if (fg_sock) {
            ssize_t chunksize = 0;
            if (!okay_to_send_bg(chunksize)) {
                return false;
            }
        }
        // after this point, we've committed to sending the
        // BG BEGIN_IROB message on the FG socket.
    }
    

    irob_id_t id = data.id;

    PendingIROB *pirob = NULL;
    if (delegate_if_necessary(id, pirob, data)) {
        // we passed it off, so make sure that we don't
        // try to re-insert data into the IROBSchedulingIndexes.
        return true;
    }
    // at this point, we know that this task is ours

    if (!pirob) {
        // must have been ACK'd already
        return true;
    }

//     pthread_mutex_unlock(&sk->scheduling_state_lock);
//     if (data.send_labels & CMM_LABEL_BACKGROUND &&
//         csock->matches(CMM_LABEL_ONDEMAND)) {
//         ssize_t chunksize = 0;
//         if (!okay_to_send_bg(chunksize)) {
//             return false;
//         }
//         // after this point, we've committed to sending the
//         // BG BEGIN_IROB message on the FG socket.
//     }

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
    csock->stats.report_send_event(id, bytes);
    int rc = writev(csock->osfd, vec, count);
    pthread_mutex_lock(&sk->scheduling_state_lock);

    delete [] deps;

    if (rc != (ssize_t)bytes) {
        sk->irob_indexes.new_irobs.insert(data);
        pthread_cond_broadcast(&sk->scheduling_state_cv);
        perror("CSocketSender: writev");
        throw CMMControlException("Socket error", hdr);
    }

    if (data.send_labels & CMM_LABEL_ONDEMAND) {
        sk->update_last_fg();
    }

    pirob = sk->outgoing_irobs.find(id);
    PendingSenderIROB *psirob = dynamic_cast<PendingSenderIROB*>(pirob);
    if (psirob) {
        psirob->announced = true;
        IROBSchedulingData new_chunk(id, true, data.send_labels);
        if (pirob->is_anonymous()) {
            csock->irob_indexes.new_chunks.insert(new_chunk);
            //csock->irob_indexes.finished_irobs.insert(data);
        } else if (pirob->chunks.size() > 0) {
            csock->irob_indexes.new_chunks.insert(new_chunk);
        }
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
        // This is probably a retransmission; must have already been ACK'd
        // Just ignore it
        return;
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
    csock->stats.report_send_event(data.id, sizeof(hdr));
    int rc = write(csock->osfd, &hdr, sizeof(hdr));
    pthread_mutex_lock(&sk->scheduling_state_lock);

    if (rc != sizeof(hdr)) {
        sk->irob_indexes.finished_irobs.insert(data);
        pthread_cond_broadcast(&sk->scheduling_state_cv);
        perror("CSocketSender: write");
        throw CMMControlException("Socket error", hdr);
    }
    
    pirob = sk->outgoing_irobs.find(data.id);
    PendingSenderIROB *psirob = dynamic_cast<PendingSenderIROB*>(pirob);
    if (psirob && psirob->is_complete()) {
        sk->ack_timeouts.update(data.id, csock->retransmission_timeout());
    }
}

/* returns true if I actually sent it; false if not */
bool
CSocketSender::irob_chunk(const IROBSchedulingData& data)
{
    // default to sending next chunk (maybe tweak to avoid extra roundtrips?)
    ssize_t chunksize = 0;
    if (data.send_labels & CMM_LABEL_BACKGROUND) {
        pthread_mutex_unlock(&sk->scheduling_state_lock);
        bool fg_sock = csock->matches(CMM_LABEL_ONDEMAND);
        pthread_mutex_lock(&sk->scheduling_state_lock);
        if (fg_sock) {
            if (!okay_to_send_bg(chunksize)) {
                return false;
            }
        }
    }

    irob_id_t id = data.id;
    PendingIROB *pirob = NULL;

    if (delegate_if_necessary(id, pirob, data)) {
        return true;
    }
    // at this point, we know that this task is ours

    if (!pirob) {
        /* must've been ACK'd already */
        return true;
    }
    PendingSenderIROB *psirob = dynamic_cast<PendingSenderIROB*>(pirob);
    assert(psirob);

    if (psirob->chunk_in_flight) {
        // another thread is sending a chunk; it will
        // signal for the next chunk to be sent when it is done
        return true;
    }
    //

    struct CMMSocketControlHdr hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.type = htons(CMM_CONTROL_MSG_IROB_CHUNK);
    hdr.send_labels = htonl(pirob->send_labels);

    u_long seqno = 0;
    size_t offset = 0;

    vector<struct iovec> irob_vecs = psirob->get_ready_bytes(chunksize, 
                                                             seqno,
                                                             offset);
    if (chunksize == 0) {
        return true;
    }

    hdr.op.irob_chunk.id = htonl(id);
    hdr.op.irob_chunk.seqno = htonl(seqno);
    hdr.op.irob_chunk.offset = htonl(offset);
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

#ifdef CMM_TIMING
    {
        PthreadScopedLock lock(&timing_mutex);
        if (timing_file) {
            struct timeval now;
            TIME(now);
            fprintf(timing_file, "%lu.%06lu CSocketSender: IROB %ld about to send %d bytes with label %lu\n", 
                    now.tv_sec, now.tv_usec, id,
                    (sizeof(hdr) + chunksize), data.send_labels);
        }
    }
#endif

    dbgprintf("About to send message: %s\n", hdr.describe().c_str());
    psirob->chunk_in_flight = true;
    pthread_mutex_unlock(&sk->scheduling_state_lock);
    csock->stats.report_send_event(id, sizeof(hdr) + chunksize);
    int rc = writev(csock->osfd, vec, irob_vecs.size() + 1);
    delete [] vec;
    pthread_mutex_lock(&sk->scheduling_state_lock);

#ifdef CMM_TIMING
    {
        PthreadScopedLock lock(&timing_mutex);
        if (timing_file) {
            struct timeval now;
            TIME(now);
            fprintf(timing_file, "%lu.%06lu CSocketSender: IROB %ld sent %d bytes with label %lu\n", 
                    now.tv_sec, now.tv_usec, id, rc, data.send_labels);
        }
    }
#endif

    // It might've been ACK'd and removed, so check first
    psirob = dynamic_cast<PendingSenderIROB*>(sk->outgoing_irobs.find(id));
    if (psirob) {
        psirob->chunk_in_flight = false;
    }

    if (rc != (ssize_t)(sizeof(hdr) + chunksize)) {
        sk->irob_indexes.new_chunks.insert(data);
        pthread_cond_broadcast(&sk->scheduling_state_cv);
        perror("CSocketSender: writev");
        throw CMMControlException("Socket error", hdr);
    }

    if (data.send_labels & CMM_LABEL_ONDEMAND) {
        sk->update_last_fg();
    }

    // It might've been ACK'd and removed, so check first
    //psirob = dynamic_cast<PendingSenderIROB*>(sk->outgoing_irobs.find(id));
    if (psirob) {
        psirob->mark_sent(chunksize);
        if (psirob->is_complete()) {
            sk->ack_timeouts.update(id, csock->retransmission_timeout());

            if (!psirob->end_announced) {
                psirob->end_announced = true;
                if (psirob->send_labels == 0) {
                    sk->irob_indexes.finished_irobs.insert(IROBSchedulingData(id, false));
                } else {
                    csock->irob_indexes.finished_irobs.insert(IROBSchedulingData(id, false));
                }
            }
        } 

        // more chunks to send, potentially, so make sure someone sends them.
        csock->irob_indexes.new_chunks.insert(data);
    }

    return true;
}

void 
CSocketSender::new_interface(struct net_interface iface)
{
    struct CMMSocketControlHdr hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.type = htons(CMM_CONTROL_MSG_NEW_INTERFACE);
    hdr.op.new_interface.ip_addr = iface.ip_addr;
    hdr.op.new_interface.labels = htonl(iface.labels);
    hdr.op.new_interface.bandwidth = htonl(iface.bandwidth);
    hdr.op.new_interface.RTT = htonl(iface.RTT);

    hdr.send_labels = htonl(0);

    dbgprintf("About to send message: %s\n", hdr.describe().c_str());
    pthread_mutex_unlock(&sk->scheduling_state_lock);
    csock->stats.report_send_event(sizeof(hdr));
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
    memset(&hdr, 0, sizeof(hdr));
    hdr.type = htons(CMM_CONTROL_MSG_DOWN_INTERFACE);
    hdr.op.down_interface.ip_addr = iface.ip_addr;

    hdr.send_labels = htonl(0);

    dbgprintf("About to send message: %s\n", hdr.describe().c_str());
    pthread_mutex_unlock(&sk->scheduling_state_lock);
    csock->stats.report_send_event(sizeof(hdr));
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

    struct timeval now, srv_time;
    TIME(now);
    if (data.completion_time.tv_sec == -1) {
        srv_time = data.completion_time;
    } else {
        TIMEDIFF(data.completion_time, now, srv_time);
    }
    hdr.op.ack.srv_time.tv_sec = htonl(srv_time.tv_sec);
    hdr.op.ack.srv_time.tv_usec = htonl(srv_time.tv_usec);
    
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
    csock->stats.report_send_event(datalen);
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
    csock->stats.report_send_event(sizeof(hdr));
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
CSocketSender::resend_request(const IROBSchedulingData& data)
{
    struct CMMSocketControlHdr hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.type = htons(CMM_CONTROL_MSG_RESEND_REQUEST);
    hdr.op.resend_request.id = htonl(data.id);
    hdr.op.resend_request.request =
        (resend_request_type_t)htonl(data.resend_request);
    hdr.op.resend_request.offset = 0;
    if (data.resend_request & CMM_RESEND_REQUEST_DATA) {
        PendingIROB *pirob = sk->incoming_irobs.find(data.id);
        PendingReceiverIROB *prirob = dynamic_cast<PendingReceiverIROB*>(pirob);
        if (prirob) {
            // tell the remote sender that we have some of the bytes
            hdr.op.resend_request.offset = htonl(prirob->recvdbytes());
        }
    }
    hdr.send_labels = htonl(csock->local_iface.labels);
    
    dbgprintf("About to send message: %s\n", hdr.describe().c_str());
    pthread_mutex_unlock(&sk->scheduling_state_lock);
    csock->stats.report_send_event(sizeof(hdr));
    int rc = write(csock->osfd, &hdr, sizeof(hdr));
    pthread_mutex_lock(&sk->scheduling_state_lock);
    
    if (rc != sizeof(hdr)) {
        sk->irob_indexes.resend_requests.insert(data);
        pthread_cond_broadcast(&sk->scheduling_state_cv);
        perror("CSocketSender: write");
        throw CMMControlException("Socket error", hdr);        
    }
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
