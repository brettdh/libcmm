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
#include <sys/socket.h>
#include <netinet/in.h>
//#include <netinet/tcp.h>
#include <linux/tcp.h>
#include <arpa/inet.h>
#include <vector>
#include <algorithm>
using std::vector; using std::max; using std::min;

#include <signal.h>
#include <errno.h>

#include "cmm_timing.h"
#include "libcmm_shmem.h"
#include "common.h"

// easy handle to enable/disable striping.
static bool striping = true;

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

    try {
        int rc = csock->phys_connect();
        if (rc < 0) {
            if (csock->oserr == ECONNREFUSED) {
                // if there's no remote listener, we won't be able to make
                //  any more connections, period.  So kill the multisocket.
                throw CMMFatalError("Remote listener is gone");
            }

            throw CMMControlException("Failed to connect new CSocket");
        }

        if (csock->matches(CMM_LABEL_ONDEMAND)) {
            csock->last_fg = sk->last_fg;
            int last_fg_secs = ipc_last_fg_tv_sec(csock);//->local_iface.ip_addr);
            ipc_set_last_fg_tv_sec(iface_pair(csock->local_iface.ip_addr,
                                              csock->remote_iface.ip_addr), //->local_iface.ip_addr, 
                                   max((int)sk->last_fg.tv_sec,
                                       last_fg_secs));
        }

        ipc_add_csocket(csock, //->local_iface.ip_addr, 
                        csock->osfd);

        PthreadScopedLock lock(&sk->scheduling_state_lock);

        // on startup, find all IROBs that might benefit from me sending
        //  some data on the new network.
//         NotCompletelySent obj;
//         sk->outgoing_irobs.for_each_by_ref(obj);
//         for (size_t i = 0; i < obj.matches.size(); ++i) {
//             IROBSchedulingData data(obj.matches[i], false);
//             csock->irob_indexes.waiting_data_checks.insert(data);
//         }

        while (1) {
            if (sk->shutting_down) {
                 if (csock->irob_indexes.waiting_acks.empty()
                     && sk->irob_indexes.waiting_acks.empty()
                     && csock->irob_indexes.resend_requests.empty()
                     && sk->irob_indexes.resend_requests.empty()
                     && sk->outgoing_irobs.empty()
                     && sk->down_local_ifaces.empty()
                     && csock->irob_indexes.waiting_data_checks.empty()
                     && sk->irob_indexes.waiting_data_checks.empty()) {
                    if (!sk->goodbye_sent && !sk->sending_goodbye) {
                        sk->sending_goodbye = true;
                        goodbye();
                    }
                    
                    if (!sk->remote_shutdown &&
                        csock->csock_recvr != NULL) {
                        struct timespec rel_timeout_ts = {
                            120, 0 // 2-minute shutdown timeout
                        };
                        struct timespec shutdown_timeout = abs_time(rel_timeout_ts);
                        
                        int rc = pthread_cond_timedwait(&sk->scheduling_state_cv,
                                                        &sk->scheduling_state_lock,
                                                        &shutdown_timeout);

                        if (rc == 0) {
                            // loop back around and check if there are acks waiting.
                            //  If I don't check this, I may hang the remote side
                            //  waiting for that one last ack, which I'll never send
                            //  because I'm waiting for the goodbye.
                            continue;
                        } else {
                            dbgprintf("Timed out waiting for goodbye; shutting down\n");
                            shutdown(csock->osfd, SHUT_RD); // tell reader to exit
                        }
                    }
                    // at this point, either we've received the remote end's
                    //  GOODBYE message, or we know we won't receive it
                    //  on this CSocket, so we're done.
                    return;
                }
            }
            
            if (csock->csock_recvr == NULL) {
                dbgprintf("Hmm, the receiver died.  Checking why.\n");
                if (sk->shutting_down && sk->goodbye_sent) {
                    throw std::runtime_error("Connection closed");
                } else {
                    throw CMMControlException("Receiver died due to "
                                              "socket error; "
                                              "sender is quitting");
                }
            }

            if (schedule_work(csock->irob_indexes) ||
                schedule_work(sk->irob_indexes)) {
                continue;
            }

            bool dropped_lock = false;
            if (!csock->is_busy()) {
                pthread_mutex_unlock(&sk->scheduling_state_lock);
                fire_thunks();
                pthread_mutex_lock(&sk->scheduling_state_lock);
                dropped_lock = true;
            }

            // resend End_IROB messages for all unACK'd IROBs whose
            //   ack timeouts have expired.
            // this will cause the receiver to send ACKs or
            //   Resend_Requests for each of them.
            vector<irob_id_t> unacked_irobs = sk->ack_timeouts.remove_expired();
            for (size_t i = 0; i < unacked_irobs.size(); ++i) {
                PendingIROB *pirob = sk->outgoing_irobs.find(unacked_irobs[i]);
                if (pirob != NULL) {
                    PendingSenderIROB *psirob = dynamic_cast<PendingSenderIROB*>(pirob);
                    assert(psirob);
                    dbgprintf("ACK timeout expired for IROB %ld, resending End_IROB\n",
                              unacked_irobs[i]);
                    IROBSchedulingData refinished_irob(unacked_irobs[i], false);
                    sk->irob_indexes.finished_irobs.insert(refinished_irob);
                    pthread_cond_broadcast(&sk->scheduling_state_cv);
                }
            }
            if (!unacked_irobs.empty()) {
                // loop back around and check whether I should 
                //  resend some of those End_IROB messages
                continue;
            }
            
            if (dropped_lock) {
                // I dropped the lock, so I should re-check the 
                // IROB scheduling indexes.
                if (schedule_work(csock->irob_indexes) ||
                    schedule_work(sk->irob_indexes)) {
                    continue;
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


            // HACK HACK HACK
            //  If there are bytes still in the socket buffer,
            //  I want to know when they are drained, so
            //  I'll poll every 10ms to check, just the same
            //  as when I'm trickling.
            // 11ms so I can tell between the two.
            struct timespec rel_thunk_timeout = { 0, 11 * 1000 * 1000 };
            struct timespec thunk_timeout = abs_time(rel_thunk_timeout);
            //if (get_unsent_bytes(csock->osfd) > 0) {
            if (ipc_total_bytes_inflight(csock) > 0) {//->local_iface.ip_addr) > 0) {
                timeout = thunk_timeout;
            }


            if (timeout.tv_sec > 0) {
                if (!timercmp(&timeout, &trickle_timeout, ==) &&
                    !timercmp(&timeout, &thunk_timeout, ==)) {
                    dbgprintf("Waiting until %lu.%09lu to check again for ACKs\n",
                              timeout.tv_sec, timeout.tv_nsec);
                }
                int rc = pthread_cond_timedwait(&sk->scheduling_state_cv,
                                                &sk->scheduling_state_lock,
                                                &timeout);
                if (timercmp(&timeout, &trickle_timeout, ==)) {
                    trickle_timeout.tv_sec = -1;
                }
                if (rc != 0 && !timercmp(&timeout, &thunk_timeout, ==)) {
                    dbgprintf("pthread_cond_timedwait failed, rc=%d\n", rc);
                }
            } else {
                pthread_cond_wait(&sk->scheduling_state_cv,
                                  &sk->scheduling_state_lock);
            }
            if (!timercmp(&timeout, &thunk_timeout, ==)) {
                dbgprintf("Woke up; maybe I can do some work?\n");
            }
            // something happened; we might be able to do some work
        }
    } catch (CMMFatalError& e) {
        dbgprintf("Fatal error on multisocket %d: %s\n",
                  sk->sock, e.what());
        
        PthreadScopedLock lock(&sk->scheduling_state_lock);
        sk->shutting_down = true;
        sk->remote_shutdown = true;
        sk->goodbye_sent = true;
        
        shutdown(sk->select_pipe[1], SHUT_RDWR);
        pthread_cond_broadcast(&sk->scheduling_state_cv);
        throw;
    } catch (CMMControlException& e) {
        //pthread_mutex_unlock(&sk->scheduling_state_lock); // no longer held here
        sk->csock_map->remove_csock(csock);
        CSocketPtr replacement = sk->csock_map->new_csock_with_labels(0);
        /*
        while (replacement && replacement->wait_until_connected() < 0) {
            if (replacement->oserr == ECONNREFUSED) {
                dbgprintf("Failed to connect replacement csocket; "
                          "connection refused, giving up\n");
                sk->csock_map->remove_csock(replacement);
                replacement.reset();
            } else {
                dbgprintf("Failed to connect replacement csocket; "
                          " %s; trying again\n",
                          strerror(replacement->oserr));
                sk->csock_map->remove_csock(replacement);
                replacement = sk->csock_map->new_csock_with_labels(0);
            }
        }
        */

        PthreadScopedLock lock(&sk->scheduling_state_lock);
        if (replacement) {
            if (replacement->is_connected()) {
                // pass off any work I didn't get to finish; it will
                //  be passed to the correct thread or deferred
                //  as appropriate
                replacement->irob_indexes.add(csock->irob_indexes);
            } else {
                sk->irob_indexes.add(csock->irob_indexes);
            }
        } else {
            // this connection is hosed, so make sure everything
            // gets cleaned up as if we had done a graceful shutdown
            dbgprintf("Connection %d is hosed; forcefully shutting down\n",
                      sk->sock);
            shutdown(sk->select_pipe[1], SHUT_RDWR);

            sk->shutting_down = true;
            sk->remote_shutdown = true;
            sk->goodbye_sent = true;
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

    if (indexes.waiting_acks.pop(data)) {
        send_acks(data, indexes);
        did_something = true;
    }

    while (indexes.waiting_data_checks.pop(data)) {
        send_data_check(data);
        did_something = true;
    }

    if (indexes.new_irobs.pop(data)) {
        irob_id_t id = data.id;
        csock->busy = true;
        if (!begin_irob(data)) {
            indexes.new_irobs.insert(data);
        } else {
            did_something = true;
            while (indexes.new_chunks.remove(id, data)) {
                irob_id_t waiting_ack_irob = -1;
                IROBSchedulingData ack;
                if (indexes.waiting_acks.pop(ack)) {
                    waiting_ack_irob = ack.id;
                }
                
                if (!irob_chunk(data, waiting_ack_irob)) {
                    indexes.new_chunks.insert(data);
                    if (waiting_ack_irob != -1) {
                        indexes.waiting_acks.insert(ack);
                    }
                    break;
                }
            }
            if (indexes.finished_irobs.remove(id, data)) {
                end_irob(data);
            }
        }
        csock->busy = false;
    }
    
    if (indexes.new_chunks.pop(data)) {
        irob_id_t waiting_ack_irob = -1;
        IROBSchedulingData ack;
        if (indexes.waiting_acks.pop(ack)) {
            waiting_ack_irob = ack.id;
        }                
        
        irob_id_t id = data.id;
        csock->busy = true;
        if (!irob_chunk(data, waiting_ack_irob)) {
            indexes.new_chunks.insert(data);
            if (waiting_ack_irob != -1) {
                indexes.waiting_acks.insert(ack);
            }
        } else {
            did_something = true;
            while (indexes.new_chunks.remove(id, data)) {
                waiting_ack_irob = -1;
                if (indexes.waiting_acks.pop(ack)) {
                    waiting_ack_irob = ack.id;
                }                

                if (!irob_chunk(data, waiting_ack_irob)) {
                    indexes.new_chunks.insert(data);
                    if (waiting_ack_irob != -1) {
                        indexes.waiting_acks.insert(ack);
                    }
                    break;
                }
            }
            if (indexes.finished_irobs.remove(id, data)) {
                end_irob(data);
            }
        }
        csock->busy = false;
    }
    
    if (indexes.finished_irobs.pop(data)) {
        csock->busy = true;
        end_irob(data);
        csock->busy = false;
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
    IROBSchedulingIndexes& indexes = (csock && csock->is_connected()
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
        pirob = sk->outgoing_irobs.find(id);
        if (!pirob) {
            // already ACK'd; don't send anything for it
            return true;
        }
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
        if (match != csock && match->is_connected()) {
            bool ret = true;
            // pass this task to the right thread
            if (!data.chunks_ready) {
                match->irob_indexes.new_irobs.insert(data);
            } else {
                match->irob_indexes.new_chunks.insert(data);
                if (striping && psirob->send_labels & CMM_LABEL_BACKGROUND) {
                    //  Try to send this chunk in parallel
                    //sk->irob_indexes.new_chunks.insert(data);
                    ret = false;
                }
            }
            pthread_cond_broadcast(&sk->scheduling_state_cv);
            return ret;
        } // otherwise, I'll do it myself (this thread)
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
        struct timeval wait_time = bg_wait_time();
        timersub(&wait_time, &time_since_last_fg, 
                 &rel_timeout);
        */
    } else {
        chunksize = csock->trickle_chunksize();

        struct timeval time_since_last_fg, now;
        struct timeval iface_last_fg;

        // get last_fg value
        iface_last_fg.tv_sec = ipc_last_fg_tv_sec(csock); //->local_iface.ip_addr);
        iface_last_fg.tv_usec = 0;
        
        TIME(now);
        
        TIMEDIFF(iface_last_fg, now, time_since_last_fg);
        if (time_since_last_fg.tv_sec > 5) {
            chunksize = csock->bandwidth();
        }
        
        // Avoid sending tiny chunks and thereby killing throughput with
        //  header overhead
        ssize_t MIN_CHUNKSIZE = 1500; // one Ethernet MTU
        chunksize = max(chunksize, MIN_CHUNKSIZE);
        
        int unsent_bytes = ipc_total_bytes_inflight(csock);//->local_iface.ip_addr);
        if (unsent_bytes < 0) {
            //dbgprintf("     ...failed to check socket send buffer: %s\n",
            //strerror(errno));
            do_trickle = false;
            //rel_timeout = bg_wait_time();
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
#ifdef CMM_TIMING
            {
                PthreadScopedLock lock(&timing_mutex);
                if (timing_file) {
                    struct timeval now;
                    TIME(now);
                    fprintf(timing_file, "%lu.%06lu  bw est %lu trickle size %zd   %d bytes in all buffers\n",
                            now.tv_sec, now.tv_usec, csock->bandwidth(), chunksize, unsent_bytes);
                }
            }
#endif
            
            //chunksize = chunksize - unsent_bytes;
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
    PendingIROB *pirob = sk->outgoing_irobs.find(data.id);
    if (!pirob) {
        // This is probably a retransmission; must have already been ACK'd
        // Just ignore it
        return true;
    }
    PendingSenderIROB *psirob = dynamic_cast<PendingSenderIROB*>(pirob);
    assert(psirob);

    irob_id_t id = data.id;

    if (!psirob->announced) {
        // only try to delegate if this is the first Begin_IROB 
        //  sent for this IROB.  Otherwise, it's a retransmission
        //  and we want to do it right away.
        if (data.send_labels & CMM_LABEL_BACKGROUND) {
            pthread_mutex_unlock(&sk->scheduling_state_lock);
            bool fg_sock = csock->is_fg();
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
    
        //PendingIROB *pirob = NULL;
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
    }

    struct CMMSocketControlHdr hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.send_labels = htonl(pirob->send_labels);

    irob_id_t *deps = NULL;

    struct iovec vec[6];
    vec[0].iov_base = &hdr;
    vec[0].iov_len = sizeof(hdr);
    vec[1].iov_base = NULL;
    vec[1].iov_len = 0;
    
    // these are for possibly sending the IROB_chunk and End_IROB 
    //  messages too for small FG IROBs
    vec[2].iov_base = NULL;
    vec[2].iov_len = 0;
    vec[3].iov_base = NULL;
    vec[3].iov_len = 0;
    vec[4].iov_base = NULL;
    vec[4].iov_len = 0;
    vec[5].iov_base = NULL;
    vec[5].iov_len = 0;

    struct iovec *vecs_to_send = vec;

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
    
    // If the IROB is small enough that it wouldn't be
    //  broken into chunks, send the IROB_chunk and End_IROB 
    //  messages now.
    // Just to be extra careful, only do this for FG IROBs.
    struct CMMSocketControlHdr chunk_hdr;
    struct CMMSocketControlHdr end_irob_hdr;
    bool sending_all_irob_info = false;
    if (psirob->is_complete() &&
        psirob->expected_bytes() < 1500 &&
        psirob->send_labels & CMM_LABEL_ONDEMAND &&
        !psirob->announced) {
        // XXX: this code is begging to be put in a function.

        memset(&chunk_hdr, 0, sizeof(chunk_hdr));
        memset(&end_irob_hdr, 0, sizeof(end_irob_hdr));
        
        chunk_hdr.type = htons(CMM_CONTROL_MSG_IROB_CHUNK);
        chunk_hdr.send_labels = htonl(pirob->send_labels);
        
        ssize_t chunksize = psirob->expected_bytes();
        u_long seqno = 0;
        size_t offset = 0;
        vector<struct iovec> irob_vecs = psirob->get_ready_bytes(chunksize, 
                                                                 seqno,
                                                                 offset);
        if (chunksize > 0) {
            chunk_hdr.op.irob_chunk.id = htonl(id);
            chunk_hdr.op.irob_chunk.seqno = htonl(seqno);
            chunk_hdr.op.irob_chunk.offset = htonl(offset);
            chunk_hdr.op.irob_chunk.datalen = htonl(chunksize);
            chunk_hdr.op.irob_chunk.data = NULL;
            count++;

            // begin_irob hdr, deps array, irob_chunk hdr, data, end_irob hdr
            vecs_to_send = new struct iovec[2 + 1 + irob_vecs.size() + 1];
            memcpy(vecs_to_send, vec, sizeof(struct iovec) * 2);
            vecs_to_send[2].iov_base = &chunk_hdr;
            vecs_to_send[2].iov_len = sizeof(chunk_hdr);
            for (size_t i = 0; i < irob_vecs.size(); ++i) {
                vecs_to_send[i + 3] = irob_vecs[i];
            }
            count += irob_vecs.size();

            end_irob_hdr.type = htons(CMM_CONTROL_MSG_END_IROB);
            end_irob_hdr.op.end_irob.id = htonl(data.id);
            end_irob_hdr.op.end_irob.expected_bytes = htonl(psirob->expected_bytes());
            end_irob_hdr.op.end_irob.expected_chunks = htonl(psirob->sent_chunks.size());
            end_irob_hdr.send_labels = htonl(pirob->send_labels);
            vecs_to_send[2 + 1 + irob_vecs.size()].iov_base = &end_irob_hdr;
            vecs_to_send[2 + 1 + irob_vecs.size()].iov_len = sizeof(end_irob_hdr);
            count++;
            
            sending_all_irob_info = true;
        }
    }

    size_t bytes = 0;
    for (size_t i = 0; i < count; ++i) {
        bytes += vecs_to_send[i].iov_len;
    }
    //vec[0].iov_len + vec[1].iov_len + vec[2].iov_len;

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
    csock->print_tcp_rto();
    int rc = writev(csock->osfd, vecs_to_send, count);
    pthread_mutex_lock(&sk->scheduling_state_lock);

    delete [] deps;
    if (sending_all_irob_info) {
        delete [] vecs_to_send;
    }

    if (rc != (ssize_t)bytes) {
        sk->irob_indexes.new_irobs.insert(data);
        pthread_cond_broadcast(&sk->scheduling_state_cv);
        if (rc < 0) {
            dbgprintf("CSocketSender: writev error: %s\n",
                      strerror(errno));
        } else {
            dbgprintf("CSocketSender: writev sent only %d of %zu bytes\n",
                      rc, bytes);
        }
        throw CMMControlException("Socket error", hdr);
    }

    if (data.send_labels & CMM_LABEL_ONDEMAND) {
        sk->update_last_fg();
        csock->update_last_fg();
    }

    pirob = sk->outgoing_irobs.find(id);
    psirob = dynamic_cast<PendingSenderIROB*>(pirob);
    if (psirob) {
        psirob->announced = true;
        if (sending_all_irob_info) {
            if (psirob->is_complete() && psirob->all_chunks_sent()) {
                sk->ack_timeouts.update(id, csock->retransmission_timeout());
                
                if (!psirob->end_announced) {
                    psirob->end_announced = true;
                    //csock->irob_indexes.finished_irobs.insert(IROBSchedulingData(id, false));
                }
            }
        } else {
            IROBSchedulingData new_chunk(id, true, data.send_labels);
            if (pirob->is_anonymous()) {
                csock->irob_indexes.new_chunks.insert(new_chunk);
                //csock->irob_indexes.finished_irobs.insert(data);
            } else if (pirob->chunks.size() > 0) {
                csock->irob_indexes.new_chunks.insert(new_chunk);
            }
        }
    }

    return true;
}

void
CSocketSender::end_irob(const IROBSchedulingData& data)
{
    PendingIROB *pirob = sk->outgoing_irobs.find(data.id);
    if (!pirob) {
        // This is probably a retransmission; must have already been ACK'd
        // Just ignore it
        return;
    }
    PendingSenderIROB *psirob = dynamic_cast<PendingSenderIROB*>(pirob);
    assert(psirob);

    struct CMMSocketControlHdr hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.type = htons(CMM_CONTROL_MSG_END_IROB);
    hdr.op.end_irob.id = htonl(data.id);
    hdr.op.end_irob.expected_bytes = htonl(psirob->expected_bytes());
    hdr.op.end_irob.expected_chunks = htonl(psirob->sent_chunks.size());
    hdr.send_labels = htonl(pirob->send_labels);

    dbgprintf("About to send message: %s\n", hdr.describe().c_str());
    pthread_mutex_unlock(&sk->scheduling_state_lock);
    csock->stats.report_send_event(data.id, sizeof(hdr));
    csock->print_tcp_rto();
    int rc = write(csock->osfd, &hdr, sizeof(hdr));
    pthread_mutex_lock(&sk->scheduling_state_lock);

    if (rc != sizeof(hdr)) {
        sk->irob_indexes.finished_irobs.insert(data);
        pthread_cond_broadcast(&sk->scheduling_state_cv);
        if (rc < 0) {
            dbgprintf("CSocketSender: write error: %s\n",
                      strerror(errno));
        } else {
            dbgprintf("CSocketSender: write sent only %d of %zu bytes\n",
                      rc, sizeof(hdr));
        }
        throw CMMControlException("Socket error", hdr);
    }
    
    pirob = sk->outgoing_irobs.find(data.id);
    psirob = dynamic_cast<PendingSenderIROB*>(pirob);
    if (psirob && psirob->is_complete()) {
        sk->ack_timeouts.update(data.id, csock->retransmission_timeout());
    }
}

/* returns true if I actually sent it; false if not */
bool
CSocketSender::irob_chunk(const IROBSchedulingData& data, irob_id_t waiting_ack_irob = -1)
{
    // default to sending next chunk (maybe tweak to avoid extra roundtrips?)
    // Cap the amount of app data we send at a time, so we can piggyback ACKs.
    //  A simple experiment indicates that this won't affect net throughput
    //  when there are no ACKs.
    const ssize_t base_chunksize = 4096;
    ssize_t chunksize = base_chunksize;
    if (data.send_labels & CMM_LABEL_BACKGROUND) {
        pthread_mutex_unlock(&sk->scheduling_state_lock);
        bool fg_sock = csock->is_fg();
        bool best_match = csock->matches(data.send_labels);
        pthread_mutex_lock(&sk->scheduling_state_lock);
        if (fg_sock || 
            (striping && (data.send_labels & CMM_LABEL_BACKGROUND) && 
             !best_match)) {
            if (!okay_to_send_bg(chunksize)) {
                return false;
            }
        }
    }
    if (sk->csock_map->count() > 1) {
        // actually enforce the maximum chunksize.
        // this should help with both striping and preemptibility.
        chunksize = min(chunksize, base_chunksize);
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

    /*
    if (psirob->chunk_in_flight) {
        // another thread is sending a chunk; it will
        // signal for the next chunk to be sent when it is done
        // XXX: this will need to be changed to enable striping.
        //return true;
        dbgprintf("Sending a chunk in parallel\n");
    }
    */

    if (psirob->needs_data_check()) {
//         struct CMMSocketControlHdr data_check_hdr;
//         memset(&data_check_hdr, 0, sizeof(data_check_hdr));
//         data_check_hdr.type = htons(CMM_CONTROL_MSG_DATA_CHECK);
//         data_check_hdr.send_labels = htonl(pirob->send_labels);
//         data_check_hdr.op.data_check.id = htonl(id);
//         pthread_mutex_unlock(&sk->scheduling_state_lock);
//         int rc = write(csock->osfd, &data_check_hdr, sizeof(data_check_hdr));
//         pthread_mutex_lock(&sk->scheduling_state_lock);
//         if (rc != sizeof(data_check_hdr)) {
//             sk->irob_indexes.new_chunks.insert(data);
//             pthread_cond_broadcast(&sk->scheduling_state_cv);
//             throw CMMControlException("Socket error", data_check_hdr);
//         }
        dbgprintf("Data check in progress for IROB %ld; waiting for response\n",
                  id);

        // when the response arrives, we'll begin sending again on this IROB.
        // until then, this thread might send data from other IROBs if there's
        //  data to send.
        return true;
    }

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

    int total_bytes = sizeof(hdr) + chunksize;
    int veccount = 1 + irob_vecs.size();
    size_t chunk_vec_index = 0;

    struct CMMSocketControlHdr ack_hdr;
    if (waiting_ack_irob != -1) {
        veccount++;
        total_bytes += sizeof(ack_hdr);
        memset(&ack_hdr, 0, sizeof(ack_hdr));
        ack_hdr.type = htons(CMM_CONTROL_MSG_ACK);
        ack_hdr.send_labels = htonl(csock->local_iface.labels);
        ack_hdr.op.ack.id = htonl(waiting_ack_irob);

    }

    struct iovec *vec = new struct iovec[veccount];
    if (waiting_ack_irob != -1) {
        // prepend ACK to this chunk
        vec[0].iov_base = &ack_hdr;
        vec[0].iov_len = sizeof(ack_hdr);
        chunk_vec_index = 1;
    }
    vec[chunk_vec_index].iov_base = &hdr;
    vec[chunk_vec_index].iov_len = sizeof(hdr);
    for (size_t i = 0; i < irob_vecs.size(); ++i) {
        // skip over the ACK (if there) and the chunk hdr
        //  and start copying the iovecs for the chunk data
        vec[i+1+chunk_vec_index] = irob_vecs[i];
    }

#ifdef CMM_TIMING
    {
        PthreadScopedLock lock(&timing_mutex);
        if (timing_file) {
            struct timeval now;
            TIME(now);
            struct tcp_info info;
            socklen_t len = sizeof(info);
            info.tcpi_rtt = 0;
            (void)getsockopt(csock->osfd, IPPROTO_TCP, TCP_INFO, &info, &len);
            fprintf(timing_file, "%lu.%06lu CSocketSender: IROB %ld about to send %u bytes with label %lu %s ",
                    now.tv_sec, now.tv_usec, id,
                    (sizeof(hdr) + chunksize), data.send_labels,
                    inet_ntoa(csock->local_iface.ip_addr));
            fprintf(timing_file, "=> %s est bw %lu rtt %lu tcpi_rtt %f\n",
                    inet_ntoa(csock->remote_iface.ip_addr),
                    csock->bandwidth(), (u_long)csock->RTT(),
                    info.tcpi_rtt / 1000000.0);
        }
    }
#endif

    dbgprintf("About to send message: %s\n", hdr.describe().c_str());
    //if (!psirob->chunk_in_flight) {
    if (striping && psirob->send_labels & CMM_LABEL_BACKGROUND &&
        sk->csock_map->count() > 1) {
        // let other threads try to send this chunk too
        sk->irob_indexes.new_chunks.insert(data);
        pthread_cond_broadcast(&sk->scheduling_state_cv);
    }
    //psirob->chunk_in_flight = true;
    pthread_mutex_unlock(&sk->scheduling_state_lock);
    if (waiting_ack_irob != -1) {
        csock->stats.report_send_event(sizeof(ack_hdr), &ack_hdr.op.ack.qdelay);
        ack_hdr.op.ack.qdelay.tv_sec = htonl(ack_hdr.op.ack.qdelay.tv_sec);
        ack_hdr.op.ack.qdelay.tv_usec = htonl(ack_hdr.op.ack.qdelay.tv_usec);
        dbgprintf("    Piggybacking ACK for IROB %ld on this message\n",
                  waiting_ack_irob);
    }
    csock->stats.report_send_event(id, sizeof(hdr) + chunksize);
    csock->print_tcp_rto();
    int rc = writev(csock->osfd, vec, veccount);
    dbgprintf("writev returned\n");
    delete [] vec;
    pthread_mutex_lock(&sk->scheduling_state_lock);
    dbgprintf("re-acquired scheduling state lock after sending irob chunk\n");

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
        //psirob->chunk_in_flight = false;

        size_t total_header_size = sizeof(hdr);
        if (waiting_ack_irob != -1) {
            total_header_size += sizeof(ack_hdr);
        }
        if (rc > (ssize_t)total_header_size) {
            // this way, I won't have to resend bytes that were
            //  already received
            //psirob->mark_sent(rc - (ssize_t)total_header_size);
        
            if (rc != (ssize_t)total_bytes) {
                // XXX: data checks are now done by the sender-side
                // when a network goes down.
                //psirob->request_data_check();
            }
        }
    }

    if (rc != (ssize_t)total_bytes) {
        sk->irob_indexes.new_chunks.insert(data);
        if (waiting_ack_irob != -1) {
            IROBSchedulingData ack(waiting_ack_irob, false);
            sk->irob_indexes.waiting_acks.insert(ack);
        }
        pthread_cond_broadcast(&sk->scheduling_state_cv);
        if (rc < 0) {
            dbgprintf("CSocketSender: writev error: %s\n",
                      strerror(errno));
        } else {
            dbgprintf("CSocketSender: writev sent only %d of %zd bytes\n",
                      rc, (ssize_t)(sizeof(hdr) + chunksize));
        }
        throw CMMControlException("Socket error", hdr);
    }

    if (data.send_labels & CMM_LABEL_ONDEMAND) {
        sk->update_last_fg();
        csock->update_last_fg();
    }

    // It might've been ACK'd and removed, so check first
    //psirob = dynamic_cast<PendingSenderIROB*>(sk->outgoing_irobs.find(id));
    if (psirob) {
        //psirob->mark_sent(chunksize);
        if (psirob->is_complete() && psirob->all_chunks_sent()) {
            sk->ack_timeouts.update(id, csock->retransmission_timeout());

            if (!psirob->end_announced) {
                psirob->end_announced = true;
                csock->irob_indexes.finished_irobs.insert(IROBSchedulingData(id, false));
            }
        } 

        // more chunks to send, potentially, so make sure someone sends them.
        if (striping && psirob->send_labels & CMM_LABEL_BACKGROUND &&
            sk->csock_map->count() > 1) {
            // already inserted it into sk->irob_indexes.new_chunks,
            //  letting another thread try to jump in.
            // So, I'll take a short break from sending it myself.
            // Either another thread has grabbed it, sent a chunk,
            //  and then notified the other threads like me,
            //  or else there is no other thread, in which case
            //  I'll grab my own sharing notification from before.
        } else {
            csock->irob_indexes.new_chunks.insert(data);
        }
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
    hdr.op.new_interface.bandwidth_down = htonl(iface.bandwidth_down);
    hdr.op.new_interface.bandwidth_up = htonl(iface.bandwidth_up);
    hdr.op.new_interface.RTT = htonl(iface.RTT);

    hdr.send_labels = htonl(0);

    dbgprintf("About to send message: %s\n", hdr.describe().c_str());
    pthread_mutex_unlock(&sk->scheduling_state_lock);
    csock->stats.report_send_event(sizeof(hdr));
    csock->print_tcp_rto();
    int rc = write(csock->osfd, &hdr, sizeof(hdr));
    pthread_mutex_lock(&sk->scheduling_state_lock);

    if (rc != sizeof(hdr)) {
        sk->changed_local_ifaces.insert(iface);
        pthread_cond_broadcast(&sk->scheduling_state_cv);
        if (rc < 0) {
            dbgprintf("CSocketSender: write error: %s\n",
                      strerror(errno));
        } else {
            dbgprintf("CSocketSender: write sent only %d of %zu bytes\n",
                      rc, sizeof(hdr));
        }
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
    csock->print_tcp_rto();
    int rc = write(csock->osfd, &hdr, sizeof(hdr));
    pthread_mutex_lock(&sk->scheduling_state_lock);

    if (rc != sizeof(hdr)) {
        sk->down_local_ifaces.insert(iface);
        pthread_cond_broadcast(&sk->scheduling_state_cv);
        if (rc < 0) {
            dbgprintf("CSocketSender: write error: %s\n",
                      strerror(errno));
        } else {
            dbgprintf("CSocketSender: write sent only %d of %zu bytes\n",
                      rc, sizeof(hdr));
        }
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
    if (data.completion_time.tv_usec == -1) {
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
    dbgprintf("About to send %zu ACKs\n", ack_count + 1);

    pthread_mutex_unlock(&sk->scheduling_state_lock);

    csock->stats.report_send_event(datalen, &hdr.op.ack.qdelay);
    hdr.op.ack.qdelay.tv_sec = htonl(hdr.op.ack.qdelay.tv_sec);
    hdr.op.ack.qdelay.tv_usec = htonl(hdr.op.ack.qdelay.tv_usec);

    csock->print_tcp_rto();
    int rc = writev(csock->osfd, vec, numvecs);
    pthread_mutex_lock(&sk->scheduling_state_lock);

    if (rc != (int)datalen) {
        // re-insert all ACKs
        sk->irob_indexes.waiting_acks.insert_range(tmp.begin(), tmp.end());

        pthread_cond_broadcast(&sk->scheduling_state_cv);
        if (rc < 0) {
            dbgprintf("CSocketSender: writev error: %s\n",
                      strerror(errno));
        } else {
            dbgprintf("CSocketSender: writev sent only %d of %zu bytes\n",
                      rc, datalen);
        }
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
    csock->print_tcp_rto();
    int rc = write(csock->osfd, &hdr, sizeof(hdr));
    pthread_mutex_lock(&sk->scheduling_state_lock);

    if (rc != sizeof(hdr)) {
        sk->sending_goodbye = false;
        pthread_cond_broadcast(&sk->scheduling_state_cv);
        if (rc < 0) {
            dbgprintf("CSocketSender: writev error: %s\n",
                      strerror(errno));
        } else {
            dbgprintf("CSocketSender: writev sent only %d of %zu bytes\n",
                      rc, sizeof(hdr));
        }
        throw CMMControlException("Socket error", hdr);
    }
    
    sk->goodbye_sent = true;
    pthread_cond_broadcast(&sk->scheduling_state_cv);
}

void
CSocketSender::resend_request(const IROBSchedulingData& data)
{
    resend_request_type_t req_type = data.resend_request;
    vector<struct irob_chunk_data> missing_chunks;
    PendingIROB *pirob = sk->incoming_irobs.find(data.id);
    PendingReceiverIROB *prirob = dynamic_cast<PendingReceiverIROB*>(pirob);
    if (req_type & CMM_RESEND_REQUEST_DATA) {
        if (prirob) {
            // tell the remote sender which bytes we need
            missing_chunks = prirob->get_missing_chunks();
        }
    }

    if (missing_chunks.empty()) {
        // no missing data; therefore, I won't request that any be resent.
        req_type = 
            resend_request_type_t(req_type 
                                  & ~CMM_RESEND_REQUEST_DATA);
    }
    if (!(req_type & CMM_RESEND_REQUEST_ALL)) {
        dbgprintf("WARNING: unnecessary data-resend requested for IROB %ld\n", 
                  data.id);
        return;
    }
        
    size_t hdrcount = missing_chunks.empty() ? 1 : missing_chunks.size();
    struct CMMSocketControlHdr *hdrs = new struct CMMSocketControlHdr[hdrcount];
    memset(hdrs, 0, sizeof(struct CMMSocketControlHdr) * hdrcount);
    hdrs[0].type = htons(CMM_CONTROL_MSG_RESEND_REQUEST);
    hdrs[0].op.resend_request.id = htonl(data.id);
    hdrs[0].op.resend_request.request =
        (resend_request_type_t)htonl(req_type);
    hdrs[0].op.resend_request.seqno = 0;
    //hdrs[0].op.resend_request.offset = 0;
    //hdrs[0].op.resend_request.len = 0;
    hdrs[0].send_labels = htonl(csock->local_iface.labels);
    if ((req_type & CMM_RESEND_REQUEST_END) && prirob) {
        hdrs[0].op.resend_request.next_chunk = htonl(prirob->next_chunk_seqno());
    }

    for (size_t i = 0; i < missing_chunks.size(); ++i) {
        hdrs[i].type = htons(CMM_CONTROL_MSG_RESEND_REQUEST);
        hdrs[i].op.resend_request.id = htonl(data.id);
        if (hdrs[i].op.resend_request.request == 0) {
            // skip the first one; it's already set
            hdrs[i].op.resend_request.request = 
                (resend_request_type_t)htonl(CMM_RESEND_REQUEST_DATA);
        }
        hdrs[i].op.resend_request.seqno = htonl(missing_chunks[i].seqno);
        //hdrs[i].op.resend_request.offset = htonl(missing_chunks[i].offset);
        //hdrs[i].op.resend_request.len = htonl(missing_chunks[i].datalen);
        hdrs[i].send_labels = htonl(csock->local_iface.labels);
    }

    size_t bytes = sizeof(hdrs[0]) * hdrcount;
    dbgprintf("About to send message: %s (and %d more)\n", 
              hdrs[0].describe().c_str(), hdrcount - 1);
    pthread_mutex_unlock(&sk->scheduling_state_lock);
    csock->stats.report_send_event(bytes);
    csock->print_tcp_rto();
    int rc = write(csock->osfd, hdrs, bytes);
    pthread_mutex_lock(&sk->scheduling_state_lock);
    
    struct CMMSocketControlHdr hdr = hdrs[0];
    delete [] hdrs;

    if (rc != (int)bytes) {
        sk->irob_indexes.resend_requests.insert(data);
        pthread_cond_broadcast(&sk->scheduling_state_cv);
        if (rc < 0) {
            dbgprintf("CSocketSender: write error: %s\n",
                      strerror(errno));
        } else {
            dbgprintf("CSocketSender: write sent only %d of %zu bytes\n",
                      rc, bytes);
        }
        throw CMMControlException("Socket error", hdr);
    }
}

void
CSocketSender::send_data_check(const IROBSchedulingData& data)
{
    struct CMMSocketControlHdr data_check_hdr;
    memset(&data_check_hdr, 0, sizeof(data_check_hdr));
    data_check_hdr.type = htons(CMM_CONTROL_MSG_DATA_CHECK);
    data_check_hdr.send_labels = 0;
    data_check_hdr.op.data_check.id = htonl(data.id);

    dbgprintf("About to send message: %s\n", 
              data_check_hdr.describe().c_str());
    pthread_mutex_unlock(&sk->scheduling_state_lock);
    csock->print_tcp_rto();
    int rc = write(csock->osfd, &data_check_hdr, sizeof(data_check_hdr));
    pthread_mutex_lock(&sk->scheduling_state_lock);
    if (rc != sizeof(data_check_hdr)) {
        dbgprintf("CSocketSender: write error: %s\n",
                  strerror(errno));
        sk->irob_indexes.waiting_data_checks.insert(data);
        pthread_cond_broadcast(&sk->scheduling_state_cv);
        throw CMMControlException("Socket error", data_check_hdr);
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
        pthread_cond_broadcast(&sk->scheduling_state_cv);
    }
    dbgprintf("Exiting.\n");

    // nobody will pthread_join to the sender now, so detach
    //  to make sure the memory gets reclaimed
    detach();

    delete this; // the last thing that will ever be done with this
}
