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

// These are weird.  I think one of them causes compile issues on one machine,
//  while the other causes issues on another.
#include <netinet/tcp.h>
//#include <linux/tcp.h>

#include <arpa/inet.h>
#include <vector>
#include <algorithm>
#include <sstream>
using std::ostringstream;
using std::vector; using std::max; using std::min;

#include <signal.h>
#include <errno.h>

#include "cmm_timing.h"
#include "libcmm_shmem.h"
#include "common.h"
#include "libcmm_net_restriction.h"

// easy handle to enable/disable striping.
static bool striping = true;

CSocketSender::CSocketSender(CSocketPtr csock_) 
  : csock(csock_), sk(get_pointer(csock_->sk)) 
{
    trickle_timeout.tv_sec = -1;
    trickle_timeout.tv_nsec = 0;
}

CSocketSender::DataInFlight::DataInFlight()
{
    data_inflight = false;
    rel_trouble_timeout.tv_sec = INT_MAX;
    rel_trouble_timeout.tv_nsec = 0;
}
int 
CSocketSender::DataInFlight::operator()(CSocketPtr csock)
{
    if (csock->data_inflight()) {
        data_inflight = true;
        struct timespec csock_rel_timeout = csock->trouble_check_timeout();
        if (timercmp(&csock_rel_timeout, &rel_trouble_timeout, <)) {
            rel_trouble_timeout = csock_rel_timeout;
        }
    }
    return 0;
}

/*
int CSocketSender::TroubleChecker::operator()(CSocketPtr csock)
{
    if (csock->data_inflight() &&
        csock->is_in_trouble() && sk->csock_map->count_locked() > 1) {
        
        struct timespec last, now, diff, timeout;
        timeout = csock->trouble_check_timeout();
        last = csock->last_trouble_check;
        TIME(now);
        if (timercmp(&last, &now, <)) {
            TIMEDIFF(csock->last_trouble_check, now, diff);
            if (timercmp(&diff, &timeout, >)) {
                // only trigger a trouble check if it's been long enough since the last one
                csock->last_trouble_check = now;
                troubled_ifaces.push_back(csock->local_iface);

                StringifyIP local_ip(&csock->local_iface.ip_addr);
                StringifyIP remote_ip(&csock->remote_iface.ip_addr);
                dbgprintf("Network (%s -> %s) is in trouble\n", local_ip.c_str(), remote_ip.c_str());
            }
        }
    }
    return 0;
}
*/

bool
CSocketSender::nothingToSend()
{
    return (csock->irob_indexes.waiting_acks.empty()
            && sk->irob_indexes.waiting_acks.empty()
            && csock->irob_indexes.resend_requests.empty()
            && sk->irob_indexes.resend_requests.empty()
            && sk->outgoing_irobs.empty()
            && sk->down_local_ifaces.empty()
            && csock->irob_indexes.waiting_data_checks.empty()
            && sk->irob_indexes.waiting_data_checks.empty());
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

            // TODO: force the scout to check if this network is really there,
            //  since I couldn't connect over it
            throw std::runtime_error("Failed to connect new CSocket");
        }

        if (csock->matches(CMM_LABEL_ONDEMAND)) {
            csock->last_fg = sk->last_fg;
            int last_fg_secs = ipc_last_fg_tv_sec(csock);
            ipc_set_last_fg_tv_sec(iface_pair(csock->local_iface.ip_addr,
                                              csock->remote_iface.ip_addr),
                                   max((int)sk->last_fg.tv_sec,
                                       last_fg_secs));
        }

        ipc_add_csocket(csock, csock->osfd);

        PthreadScopedLock lock(&sk->scheduling_state_lock);

        while (1) {
            if (sk->shutting_down && nothingToSend()) {
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

            // Sending FG data will get redirected to a non-troubled sender
            //  in delegate_if_necessary.
            // I might still send some other things, such as BG data.
            // if I do, other threads can work on sending my other items.
            //   potentially: acks, resend requests, iface updates, etc.

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

            if (dropped_lock) {
                // I dropped the lock, so I should re-check the 
                // IROB scheduling indexes.
                if (schedule_work(csock->irob_indexes) ||
                    schedule_work(sk->irob_indexes)) {
                    continue;
                }
            }
            
            struct timespec timeout = {-1, 0};

            /*
            DataInFlight inflight;
            sk->csock_map->for_each(inflight);
            if ((inflight.data_inflight && inflight.rel_trouble_timeout.tv_sec != -1)) {
                timeout = abs_time(inflight.rel_trouble_timeout);
                dbgprintf("Data in flight; trouble-check timeout in %lu.%09lu sec\n",
                          inflight.rel_trouble_timeout.tv_sec,
                          inflight.rel_trouble_timeout.tv_nsec);
            }
            */
            
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
            if (ipc_total_bytes_inflight(csock) > 0 && count_thunks(sk->sock) > 0) {
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

            /* TODO-REDUNDANCY: this is the periodic-reevaluation part.
             * TODO-REDUNDANCY:  (only applicable for my redundancy code.) */
            // TODO-REDUNDANCY: make sure that I get woken up to do this check.
            // TODO-REDUNDANCY: maybe use a timeout, only calculated and set
            // TODO-REDUNDANCY:   if we have sent >1 IROB non-redundantly.
            /*
            TroubleChecker checker(sk);
            sk->csock_map->for_each(checker);
            if (checker.troubled_ifaces.size() > 0) {
                dbgprintf("Network(s) in trouble; data-checking FG IROBs\n");
                
                for (size_t i = 0; i < checker.troubled_ifaces.size(); ++i) {
                    sk->data_check_all_irobs(checker.troubled_ifaces[i].ip_addr.s_addr, 0, 
                                             CMM_LABEL_ONDEMAND);
                }
            }
            */
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
        PthreadScopedRWLock sock_lock(&sk->my_lock, true);
        sk->csock_map->remove_csock(csock);
        if (sk->accepting_side) {
            // don't try to make a new connection; 
            //  the connecting side will do it
            PthreadScopedLock lock(&sk->scheduling_state_lock);
            sk->irob_indexes.add(csock->irob_indexes);
            csock->stats.mark_irob_failures(sk->csock_map->get_network_chooser(), 
                                            csock->network_type());
            sk->data_check_all_irobs(csock->local_iface.ip_addr.s_addr);
            if (sk->csock_map->empty()) {
                // no more connections; kill the multisocket
                dbgprintf("Multisocket %d has no more csockets and I'm the accepting side; "
                          "shutting down\n", sk->sock);
                sk->shutting_down = true;
                sk->remote_shutdown = true;
                sk->goodbye_sent = true;
                shutdown(sk->select_pipe[1], SHUT_RDWR);
            }
            pthread_cond_broadcast(&sk->scheduling_state_cv);
            throw;
        }
        CSocketPtr replacement = sk->csock_map->new_csock_with_labels(0);

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
            csock->stats.mark_irob_failures(sk->csock_map->get_network_chooser(), 
                                            csock->network_type());
            sk->data_check_all_irobs(csock->local_iface.ip_addr.s_addr);
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
    ASSERT(op);
    u_long send_labels = 0;
    {
        PthreadScopedLock lock(&op->sk->scheduling_state_lock);
        PendingIROBPtr pirob = op->sk->outgoing_irobs.find(op->data.id);
        ASSERT(pirob);
        PendingSenderIROB *psirob = dynamic_cast<PendingSenderIROB*>(get_pointer(pirob));
        ASSERT(psirob);
        send_labels = psirob->get_send_labels();
    }

    CSocketPtr csock;
    if (op->sk->accepting_side) {
        csock = op->sk->csock_map->connected_csock_with_labels(send_labels);
    } else {
        csock = op->sk->csock_map->new_csock_with_labels(send_labels);
    }
    
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

bool
CSocketSender::delegate_resend_request_if_necessary(irob_id_t id, PendingReceiverIROB *prirob, 
                                                    const IROBSchedulingData& data)
{
    // TODO: check whether I should be sending this.
    // TODO: only thing to check, really, is the net restriction labels.
    // TODO: otherwise, I wasn't checking at all before.
    // TODO: I think this only gets invoked when a network goes down, anyway.

    u_long send_labels = prirob->get_send_labels();
    if (!csock->fits_net_restriction(send_labels)) {
        pthread_mutex_unlock(&sk->scheduling_state_lock);

        CSocketPtr match;
        if (sk->accepting_side) {
            match = sk->csock_map->connected_csock_with_labels(send_labels);
        } else {
            match = sk->csock_map->new_csock_with_labels(send_labels);
        }

        pthread_mutex_lock(&sk->scheduling_state_lock);

        PendingIROBPtr pirob = sk->incoming_irobs.find(id);
        if (!pirob) {
            // it got ACKEd while I was checking.  no need for the resend request.
            return true;
        }
        
        if (match) {
            match->irob_indexes.resend_requests.insert(data);
        } else {
            // it gets dropped.  oh well.
            //   it was restricted to a network type that's now gone. 
            dbgprintf("Warning: silently dropping resend request for IROB %ld\n", id);
        }

        return true;
    }
    return false;
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
CSocketSender::delegate_if_necessary(irob_id_t id, PendingIROBPtr& pirob, 
                                     const IROBSchedulingData& data)
{
    pirob = sk->outgoing_irobs.find(id);
    if (!pirob) {
        // already ACK'd; don't send anything for it
        return true;
    }
    ASSERT(pirob);

    u_long send_labels = pirob->get_send_labels();

    PendingSenderIROB *psirob = dynamic_cast<PendingSenderIROB*>(get_pointer(pirob));
    ASSERT(psirob);

    size_t num_bytes = 0;
    if (psirob->is_complete()) {
        num_bytes = psirob->expected_bytes();
    }
    bool redundant = psirob->should_send_on_all_networks();

    pthread_mutex_unlock(&sk->scheduling_state_lock);

    if (csock->matches(send_labels, num_bytes)) {
        pthread_mutex_lock(&sk->scheduling_state_lock);
        pirob = sk->outgoing_irobs.find(id);
        if (!pirob) {
            // already ACK'd; don't send anything for it
            return true;
        }

        if (redundant) {
            // let other senders know about this too.
            sk->csock_map->pass_request_to_all_senders(psirob, data);
            pthread_cond_broadcast(&sk->scheduling_state_cv);
        }

        return false;
    }
    
    CSocketPtr match;
    if (sk->accepting_side) {
        match = sk->csock_map->connected_csock_with_labels(send_labels,
                                                           num_bytes);
    } else {
        match = sk->csock_map->new_csock_with_labels(send_labels, num_bytes);
    }

    pthread_mutex_lock(&sk->scheduling_state_lock);

    pirob = sk->outgoing_irobs.find(id);
    if (!pirob) {
        // just kidding; the ACK arrived while I wasn't holding the lock
        // no need to send this message after all
        return true;
    }
    psirob = dynamic_cast<PendingSenderIROB*>(get_pointer(pirob));
    ASSERT(psirob);

    if (!match) {
        if (!csock->fits_net_restriction(send_labels)) {
            // no network is available that meets these restrictions.
            dbgprintf("Can't satisfy network restrictions (%s) for "
                      "IROB %ld, so I'm dropping it\n",
                      describe_network_restrictions(send_labels).c_str(),
                      pirob->get_id());
            
            pirob->set_status(CMM_FAILED);
            if (pirob->is_complete()) {
                // no more application calls coming for this IROB;
                //   it can safely be forgotten
                sk->outgoing_irobs.drop_irob_and_dependents(pirob->get_id());
                pirob.reset();
            } // else: mc_end_irob hasn't returned yet; it will clean up
            
            return true;
        }

        // if this csocket fits the network restrictions, we might want to
        //  send data even though it's not the best network.

        if (send_labels & CMM_LABEL_BACKGROUND) {
            // No actual background network, so let's trickle 
            // on the available network
            return false;
        }

        if (redundant) {
            // we decided earlier to send this redundantly,
            //  so every CSocket should send it.
            return false;
        }

        if (!pirob->is_complete()) {
            // mc_end_irob hasn't returned yet for this IROB;
            //  we can still tell the application it failed
            //  or otherwise block/thunk
            resume_handler_t resume_handler = NULL;
            void *rh_arg = NULL;
            psirob->get_thunk(resume_handler, rh_arg);
            if (resume_handler) {
                enqueue_handler(sk->sock,
                                pirob->get_send_labels(), 
                                resume_handler, rh_arg);
                pirob->set_status(CMM_DEFERRED);
            } else {
                enqueue_handler(sk->sock,
                                pirob->get_send_labels(), 
                                (resume_handler_t)resume_operation_thunk,
                                new ResumeOperation(sk, data));
                pirob->set_status(CMM_BLOCKING);
            }
        } else {
            /* no way to tell the application that we failed to send
             * this IROB.  Just drop it, and let the application 
             * do the usual end-to-end check (e.g. response timeout). 
             */
            dbgprintf("Warning: silently dropping IROB %ld after failing to send it.\n",
                      pirob->get_id());
            sk->outgoing_irobs.drop_irob_and_dependents(pirob->get_id());
            pirob.reset();
        }
        return true;
    } else {
        StringifyIP my_local_ip(&csock->local_iface.ip_addr);
        StringifyIP my_remote_ip(&csock->remote_iface.ip_addr);
        StringifyIP match_local_ip(&match->local_iface.ip_addr);
        StringifyIP match_remote_ip(&match->remote_iface.ip_addr);
        dbgprintf("Deciding to send %s for IROB %d on socket %d (%s -> %s)\n",
                  data.chunks_ready ? "data" : "metadata", (int) id,
                  match->osfd, match_local_ip.c_str(), match_remote_ip.c_str());
        
        if (match != csock && 
            (match->is_connected() || !csock->fits_net_restriction(send_labels))) {
            bool ret = true;
            // pass this task to the right thread
            if (data.data_check) {
                match->irob_indexes.waiting_data_checks.insert(data);
            } else if (!data.chunks_ready) {
                match->irob_indexes.new_irobs.insert(data);
            } else {
                match->irob_indexes.new_chunks.insert(data);
                if (striping && psirob->get_send_labels() & CMM_LABEL_BACKGROUND &&
                    csock->fits_net_restriction(psirob->get_send_labels())) {
                    //  Try to send this chunk in parallel
                    ret = false;
                }
            }
            pthread_cond_broadcast(&sk->scheduling_state_cv);

            if (redundant && csock->fits_net_restriction(psirob->get_send_labels())) {
                ret = false;
            }
            if (!ret) {
                dbgprintf("Deciding to also send %s for IROB %d on socket %d (%s -> %s)\n",
                          data.chunks_ready ? "data" : "metadata", (int) id,
                          csock->osfd, my_local_ip.c_str(), my_remote_ip.c_str());
            }

            return ret;
        } // otherwise, I'll do it myself (this thread)
    }

    return false;
}

static ssize_t MIN_CHUNKSIZE = 1500; // one Ethernet MTU

bool CSocketSender::okay_to_send_bg(ssize_t& chunksize)
{
    bool do_trickle = true;

    //dbgprintf("Checking whether to trickle background data...\n");

    struct timeval rel_timeout;
    
    chunksize = csock->trickle_chunksize();

    struct timeval time_since_last_fg, now;
    struct timeval iface_last_fg;

    // get last_fg value
    iface_last_fg.tv_sec = ipc_last_fg_tv_sec(csock);
    iface_last_fg.tv_usec = 0;
        
    TIME(now);
        
    TIMEDIFF(iface_last_fg, now, time_since_last_fg);
    if (time_since_last_fg.tv_sec > 5) {
        chunksize = csock->bandwidth();
    }
        
    // Avoid sending tiny chunks and thereby killing throughput with
    //  header overhead
    chunksize = max(chunksize, MIN_CHUNKSIZE);
        
    int unsent_bytes = ipc_total_bytes_inflight(csock);//->local_iface.ip_addr);
    if (unsent_bytes < 0) {
        //dbgprintf("     ...failed to check socket send buffer: %s\n",
        //strerror(errno));
        do_trickle = false;
    } else if (unsent_bytes >= chunksize) {
        /*dbgprintf("     ...socket buffer has %d bytes left; more than %d\n",
          unsent_bytes, csock->trickle_chunksize());*/
        do_trickle = false;
    } else {
#ifdef CMM_TIMING
        {
            PthreadScopedLock lock(&timing_mutex);
            if (timing_file) {
                struct timeval now;
                TIME(now);
                fprintf(timing_file, "%lu.%06lu  bw est %lu trickle size %d   %d bytes in all buffers\n",
                        now.tv_sec, now.tv_usec, csock->bandwidth(), (int)chunksize, unsent_bytes);
            }
        }
#endif
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
    PendingIROBPtr pirob = sk->outgoing_irobs.find(data.id);
    if (!pirob) {
        // This is probably a retransmission; must have already been ACK'd
        // Just ignore it
        return true;
    }
    PendingSenderIROB *psirob = dynamic_cast<PendingSenderIROB*>(get_pointer(pirob));
    ASSERT(psirob);

    irob_id_t id = data.id;

    if (!csock->fits_net_restriction(psirob->get_send_labels())) {
        if (delegate_if_necessary(id, pirob, data) 
            || !pirob) { // checked after return; could have been modified
            return true;
        }
    }

    // XXX: this is problematic, I think.  Now that
    // XXX:  'was_announced' is per-CSocket, I'm getting multiple
    // XXX:  announcements on the same CSocket and none at all on others, sometimes.
    // XXX:  need to consider loss+retransmission separately, I think.
    if (!psirob->was_announced(get_pointer(csock))) {
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
    hdr.send_labels = htonl(pirob->get_send_labels());

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

    irob_id_t *deps = NULL;
    int numdeps = pirob->copy_deps_htonl(&deps);
    
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
        psirob->expected_bytes() < (size_t) MIN_CHUNKSIZE &&
        psirob->get_send_labels() & CMM_LABEL_ONDEMAND &&
        !psirob->was_announced(get_pointer(csock))) {
        // XXX: this code is begging to be put in a function.

        memset(&chunk_hdr, 0, sizeof(chunk_hdr));
        memset(&end_irob_hdr, 0, sizeof(end_irob_hdr));
        
        chunk_hdr.type = htons(CMM_CONTROL_MSG_IROB_CHUNK);
        chunk_hdr.send_labels = htonl(pirob->get_send_labels());
        
        ssize_t chunksize = psirob->expected_bytes();
        u_long seqno = 0;
        size_t offset = 0;
        vector<struct iovec> irob_vecs = 
            psirob->get_ready_bytes(get_pointer(csock), chunksize, seqno, offset);
        if (chunksize > 0) {
            chunk_hdr.op.irob_chunk.id = htonl(id);
            chunk_hdr.op.irob_chunk.seqno = htonl(seqno);
            chunk_hdr.op.irob_chunk.offset = htonl(offset);
            chunk_hdr.op.irob_chunk.datalen = htonl(chunksize);
            chunk_hdr.op.irob_chunk.data = NULL;
            count++;

            csock->update_net_restriction_stats(pirob->get_send_labels(), chunksize, 0);

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
            end_irob_hdr.op.end_irob.expected_chunks = 
                htonl(psirob->num_sender_chunks());
            end_irob_hdr.send_labels = htonl(pirob->get_send_labels());
            vecs_to_send[2 + 1 + irob_vecs.size()].iov_base = &end_irob_hdr;
            vecs_to_send[2 + 1 + irob_vecs.size()].iov_len = sizeof(end_irob_hdr);
            count++;
            
            sending_all_irob_info = true;
        }

        // because it is very small, and we're sending all of its data now
        ASSERT(psirob->all_bytes_chunked());
        csock->stats.report_total_irob_bytes(data.id, psirob->get_total_network_bytes());
    }

    size_t bytes = 0;
    for (size_t i = 0; i < count; ++i) {
        bytes += vecs_to_send[i].iov_len;
    }

    ostringstream s;
    s << "About to send message: " << hdr.describe();
    if (deps) {
        s << " deps [ ";
        for (int i = 0; i < numdeps; ++i) {
            s << ntohl(deps[i]) << " ";
        }
        s << "]";
    }
    dbgprintf("%s\n", s.str().c_str());
    if (sending_all_irob_info) {
        dbgprintf("About to send message: %s\n", chunk_hdr.describe().c_str());
        dbgprintf("About to send message: %s\n", end_irob_hdr.describe().c_str());
    }

    if (data.send_labels & CMM_LABEL_ONDEMAND) {
        sk->update_last_fg();
        csock->update_last_fg();
    }
    csock->update_last_app_data_sent();
    psirob->markSentOn(csock);

    pthread_mutex_unlock(&sk->scheduling_state_lock);
    csock->stats.report_irob_send_event(id, bytes);
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

    pirob = sk->outgoing_irobs.find(id);
    psirob = dynamic_cast<PendingSenderIROB*>(get_pointer(pirob));
    if (psirob) {
        psirob->mark_announcement_sent(get_pointer(csock));
        if (sending_all_irob_info) {
            if (psirob->is_complete() && psirob->all_bytes_chunked()) {
                psirob->mark_end_announcement_sent(get_pointer(csock));
            }
        } else {
            IROBSchedulingData new_chunk(id, true, data.send_labels);
            if (pirob->is_anonymous() || pirob->get_num_chunks() > 0) {
                csock->irob_indexes.new_chunks.insert(new_chunk);
            }
        }
    }

    return true;
}

void
CSocketSender::end_irob(const IROBSchedulingData& data)
{
    PendingIROBPtr pirob = sk->outgoing_irobs.find(data.id);
    if (!pirob) {
        // This is probably a retransmission; must have already been ACK'd
        // Just ignore it
        return;
    }
    PendingSenderIROB *psirob = dynamic_cast<PendingSenderIROB*>(get_pointer(pirob));
    ASSERT(psirob);

    struct CMMSocketControlHdr hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.type = htons(CMM_CONTROL_MSG_END_IROB);
    hdr.op.end_irob.id = htonl(data.id);
    hdr.op.end_irob.expected_bytes = htonl(psirob->expected_bytes());
    hdr.op.end_irob.expected_chunks = htonl(psirob->num_sender_chunks());
    hdr.send_labels = htonl(pirob->get_send_labels());

    csock->update_last_app_data_sent();
    psirob->markSentOn(csock);
    if (psirob->is_complete() && psirob->all_bytes_chunked()) {
        csock->stats.report_total_irob_bytes(data.id, psirob->get_total_network_bytes());
    }

    dbgprintf("About to send message: %s\n", hdr.describe().c_str());
    pthread_mutex_unlock(&sk->scheduling_state_lock);
    csock->stats.report_irob_send_event(data.id, sizeof(hdr));
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
    psirob = dynamic_cast<PendingSenderIROB*>(get_pointer(pirob));
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
    PendingIROBPtr pirob;

    if (delegate_if_necessary(id, pirob, data)) {
        return true;
    }
    // at this point, we know that this task is ours

    if (!pirob) {
        /* must've been ACK'd already */
        return true;
    }
    PendingSenderIROB *psirob = dynamic_cast<PendingSenderIROB*>(get_pointer(pirob));
    ASSERT(psirob);

    struct CMMSocketControlHdr hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.type = htons(CMM_CONTROL_MSG_IROB_CHUNK);
    hdr.send_labels = htonl(pirob->get_send_labels());

    u_long seqno = 0;
    size_t offset = 0;

    vector<struct iovec> irob_vecs = 
        psirob->get_ready_bytes(get_pointer(csock), chunksize, seqno, offset);
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
        ack_hdr.send_labels = 0;
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
            fprintf(timing_file, "%lu.%06lu CSocketSender: IROB %ld about to send %d bytes with label %lu %s ",
                    now.tv_sec, now.tv_usec, id,
                    (int)(sizeof(hdr) + chunksize), data.send_labels,
                    StringifyIP(&csock->local_iface.ip_addr).c_str());
            fprintf(timing_file, "=> %s est bw %lu rtt %lu tcpi_rtt %f\n",
                    StringifyIP(&csock->remote_iface.ip_addr).c_str(),
                    csock->bandwidth(), (u_long)csock->RTT(),
                    info.tcpi_rtt / 1000000.0);
        }
    }
#endif

    dbgprintf("About to send message: %s\n", hdr.describe().c_str());
    if (striping && psirob->get_send_labels() & CMM_LABEL_BACKGROUND &&
        !has_network_restriction(psirob->get_send_labels()) &&
        sk->csock_map->count() > 1) {
        // let other threads try to send this chunk too
        sk->irob_indexes.new_chunks.insert(data);
        pthread_cond_broadcast(&sk->scheduling_state_cv);
    }

    csock->update_net_restriction_stats(psirob->get_send_labels(), chunksize, 0);
    
    if (data.send_labels & CMM_LABEL_ONDEMAND) {
        sk->update_last_fg();
        csock->update_last_fg();
    }
    csock->update_last_app_data_sent();
    psirob->markSentOn(csock);

    if (psirob->is_complete() && psirob->all_bytes_chunked()) {
        csock->stats.report_total_irob_bytes(id, psirob->get_total_network_bytes());
    }

    // wake up other sleeping threads to check whether I might be in trouble
    //  (just in case I block on the writev and don't wake up for a while)
    pthread_cond_broadcast(&sk->scheduling_state_cv);
    
    pthread_mutex_unlock(&sk->scheduling_state_lock);
    if (waiting_ack_irob != -1) {
        csock->stats.report_non_irob_send_event(sizeof(ack_hdr), &ack_hdr.op.ack.qdelay);
        ack_hdr.op.ack.qdelay.tv_sec = htonl(ack_hdr.op.ack.qdelay.tv_sec);
        ack_hdr.op.ack.qdelay.tv_usec = htonl(ack_hdr.op.ack.qdelay.tv_usec);
        dbgprintf("    Piggybacking ACK for IROB %ld on this message\n",
                  waiting_ack_irob);
    }
    csock->stats.report_irob_send_event(id, sizeof(hdr) + chunksize);
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
    pirob = sk->outgoing_irobs.find(id);
    psirob = dynamic_cast<PendingSenderIROB*>(get_pointer(pirob));
    if (psirob) {
        size_t total_header_size = sizeof(hdr);
        if (waiting_ack_irob != -1) {
            total_header_size += sizeof(ack_hdr);
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
            dbgprintf("CSocketSender: writev sent only %d of %d bytes\n",
                      rc, (int)(sizeof(hdr) + chunksize));
        }
        throw CMMControlException("Socket error", hdr);
    }

    // It might've been ACK'd and removed, so check first
    //psirob = dynamic_cast<PendingSenderIROB*>(sk->outgoing_irobs.find(id));
    if (psirob) {
        if (psirob->is_complete() && psirob->all_bytes_chunked()) {
            if (!psirob->end_was_announced(get_pointer(csock))) {
                csock->irob_indexes.finished_irobs.insert(IROBSchedulingData(id, false));
            }
        } 

        // more chunks to send, potentially, so make sure someone sends them.
        if (striping && psirob->get_send_labels() & CMM_LABEL_BACKGROUND &&
            !has_network_restriction(psirob->get_send_labels()) &&
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
    hdr.op.new_interface.bandwidth_down = htonl(iface.bandwidth_down);
    hdr.op.new_interface.bandwidth_up = htonl(iface.bandwidth_up);
    hdr.op.new_interface.RTT = htonl(iface.RTT);
    hdr.op.new_interface.type = htonl(iface.type);

    hdr.send_labels = htonl(0);

    csock->update_last_app_data_sent();

    dbgprintf("About to send message: %s\n", hdr.describe().c_str());
    pthread_mutex_unlock(&sk->scheduling_state_lock);
    csock->stats.report_non_irob_send_event(sizeof(hdr));
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
    csock->stats.report_non_irob_send_event(sizeof(hdr));
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
    hdr.send_labels = 0;
    hdr.op.ack.id = htonl(data.id);

    struct timeval now, srv_time;
    TIME(now);
    if (data.completion_time.tv_sec == 0 || /* no completion time; let srv_time be zero */
        data.completion_time.tv_usec == -1 /* mark invalid for measurement */) {
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

    csock->update_last_app_data_sent();

    pthread_mutex_unlock(&sk->scheduling_state_lock);

    csock->stats.report_non_irob_send_event(datalen, &hdr.op.ack.qdelay);
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
    hdr.send_labels = 0;

    csock->update_last_app_data_sent();

    dbgprintf("About to send message: %s\n", hdr.describe().c_str());
    pthread_mutex_unlock(&sk->scheduling_state_lock);
    csock->stats.report_non_irob_send_event(sizeof(hdr));
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
    PendingIROBPtr pirob = sk->incoming_irobs.find(data.id);
    PendingReceiverIROB *prirob = dynamic_cast<PendingReceiverIROB*>(get_pointer(pirob));
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

    if (prirob) { 
        if (delegate_resend_request_if_necessary(data.id, prirob, data)) {
            // should try resend requests on label-matched thread first
            //  also, shouldn't data-check contrary to net restriction labels
            return;
        }
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
    hdrs[0].send_labels = 0;
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
        hdrs[i].send_labels = 0;
    }

    csock->update_last_app_data_sent();

    size_t bytes = sizeof(hdrs[0]) * hdrcount;
    dbgprintf("About to send message: %s (and %d more)\n", 
              hdrs[0].describe().c_str(), hdrcount - 1);
    pthread_mutex_unlock(&sk->scheduling_state_lock);
    csock->stats.report_non_irob_send_event(bytes);
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

    // prepend the metadata and last data chunk for this IROB,
    //  so as to avoid incurring an extra RTT for small losses.
    struct iovec *vecs = NULL;
    struct CMMSocketControlHdr begin_irob_hdr, irob_chunk_hdr, end_irob_hdr;
    memset(&begin_irob_hdr, 0, sizeof(begin_irob_hdr));
    memset(&irob_chunk_hdr, 0, sizeof(irob_chunk_hdr));
    memset(&end_irob_hdr, 0, sizeof(end_irob_hdr));
    size_t vecs_count = 0;
    irob_id_t *deps = NULL;
    struct irob_chunk_data chunk;
    memset(&chunk, 0, sizeof(chunk));

    PendingIROBPtr pirob = sk->outgoing_irobs.find(data.id);
    PendingSenderIROB *psirob = dynamic_cast<PendingSenderIROB*>(get_pointer(pirob));
    if (psirob) { 
        if (delegate_if_necessary(data.id, pirob, data)) {
            // should try data checks on label-matched thread first
            //  also, shouldn't data-check contrary to net restriction labels
            return;
        }

        data_check_hdr.send_labels = htonl(pirob->get_send_labels());
        ssize_t chunksize = MIN_CHUNKSIZE;
        u_long seqno = 0;
        size_t offset = 0;
        vector<struct iovec> chunk_vecs = 
            psirob->get_ready_bytes(get_pointer(csock), chunksize, seqno, offset);
        if (chunk_vecs.empty()) {
            // if there's no next chunk to send, re-send the last sent chunk.
            chunk_vecs = psirob->get_last_sent_chunk_htonl(get_pointer(csock), &chunk);
        } else {
            chunk.id = htonl(data.id);
            chunk.seqno = htonl(seqno);
            chunk.offset = htonl(offset);
            chunk.datalen = htonl(chunksize);
        }

        // (begin, deps, chunk_hdr), data, (end, data_check)
        vecs = new struct iovec[3 + chunk_vecs.size() + 2];

        int numdeps = psirob->copy_deps_htonl(&deps);

        // TODO: make this all a function.
        begin_irob_hdr.type = htons(CMM_CONTROL_MSG_BEGIN_IROB);
        begin_irob_hdr.op.begin_irob.id = htonl(data.id);
        begin_irob_hdr.op.begin_irob.numdeps = htonl(numdeps);
        begin_irob_hdr.send_labels = htonl(psirob->get_send_labels());
        vecs[vecs_count].iov_base = &begin_irob_hdr;
        vecs[vecs_count].iov_len = sizeof(begin_irob_hdr);
        vecs_count++;

        if (deps) {
            vecs[vecs_count].iov_base = deps;
            vecs[vecs_count].iov_len = numdeps * sizeof(irob_id_t);
            vecs_count++;
        }
        
        if (!chunk_vecs.empty()) {
            irob_chunk_hdr.type = htons(CMM_CONTROL_MSG_IROB_CHUNK);
            irob_chunk_hdr.op.irob_chunk = chunk;
            irob_chunk_hdr.op.irob_chunk.data = NULL;
            irob_chunk_hdr.send_labels = htonl(psirob->get_send_labels());
            
            vecs[vecs_count].iov_base = &irob_chunk_hdr;
            vecs[vecs_count].iov_len = sizeof(irob_chunk_hdr);
            vecs_count++;

            for (size_t i = 0; i < chunk_vecs.size(); ++i) {
                vecs[vecs_count] = chunk_vecs[i];
                vecs_count++;
            }
        }
        
        if (psirob->is_complete() && psirob->all_bytes_chunked()) {
            // TODO: make this a function.
            end_irob_hdr.type = htons(CMM_CONTROL_MSG_END_IROB);
            end_irob_hdr.op.end_irob.id = htonl(data.id);
            end_irob_hdr.op.end_irob.expected_bytes = htonl(psirob->expected_bytes());
            end_irob_hdr.op.end_irob.expected_chunks = htonl(psirob->num_sender_chunks());
            end_irob_hdr.send_labels = htonl(psirob->get_send_labels());
            
            vecs[vecs_count].iov_base = &end_irob_hdr;
            vecs[vecs_count].iov_len = sizeof(end_irob_hdr);
            vecs_count++;

            csock->stats.report_total_irob_bytes(data.id, psirob->get_total_network_bytes());
        }
    } else {
        // must have been ACKed, sometime between enqueuing a
        //  data-check and now.
        // This data-check probably doesn't need to be sent at all,
        //  but I'm not 100% sure about that, so I'll leave it
        //  alone for now.  The effect will be a harmless dup-ack.

        ASSERT(vecs_count == 0);
        vecs = new struct iovec[1];
    }
    vecs[vecs_count].iov_base = &data_check_hdr;
    vecs[vecs_count].iov_len = sizeof(data_check_hdr);
    vecs_count++;
    
    size_t bytes_to_send = 0;
    for (size_t i = 0; i < vecs_count; ++i) {
        bytes_to_send += vecs[i].iov_len;
    }

    csock->update_last_app_data_sent();
    if (psirob) {
        psirob->markSentOn(csock);
    }
    csock->stats.report_irob_send_event(data.id, bytes_to_send);

    dbgprintf("About to send message: %s\n", 
              data_check_hdr.describe().c_str());
    pthread_mutex_unlock(&sk->scheduling_state_lock);
    csock->print_tcp_rto();
    int rc = writev(csock->osfd, vecs, vecs_count);
    delete [] deps;
    delete [] vecs;
    pthread_mutex_lock(&sk->scheduling_state_lock);
    if (rc != (int) bytes_to_send) {
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
        PthreadScopedRWLock sock_lock(&sk->my_lock, true);
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
