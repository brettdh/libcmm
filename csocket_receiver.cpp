#include <errno.h>
#include "csocket_receiver.h"
#include "csocket.h"
#include "timeops.h"
#include "debug.h"
#include "pending_irob.h"
#include "pending_receiver_irob.h"
#include "cmm_socket.private.h"
#include "csocket_mapping.h"
#include "pthread_util.h"
#include "cmm_timing.h"
#include "libcmm_shmem.h"
#include "network_chooser.h"

#include <sstream>
using std::ostringstream;

CSocketReceiver::handler_fn_t CSocketReceiver::handlers[] = {
    &CSocketReceiver::unrecognized_control_msg, /* HELLO not expected */
    &CSocketReceiver::do_begin_irob,
    &CSocketReceiver::do_end_irob,
    &CSocketReceiver::do_irob_chunk,

    //&CSocketReceiver::do_default_irob,
    &CSocketReceiver::unrecognized_control_msg, /* DEFAULT_IROB not expected */

    &CSocketReceiver::do_new_interface,
    &CSocketReceiver::do_down_interface,
    &CSocketReceiver::do_ack,
    &CSocketReceiver::do_goodbye,
    &CSocketReceiver::do_request_resend,
    &CSocketReceiver::do_data_check
};

CSocketReceiver::CSocketReceiver(CSocketPtr csock_) 
    : csock(csock_), sk(csock_->sk.get()) {}

void
CSocketReceiver::unrecognized_control_msg(struct CMMSocketControlHdr hdr)
{
    throw CMMControlException("Unrecognized control message", hdr);
}

void
CSocketReceiver::dispatch(struct CMMSocketControlHdr hdr)
{
    short type = ntohs(hdr.msgtype());
    if (type < 0 || type >= (short)sizeof(handlers)) {
        unrecognized_control_msg(hdr);
    } else {
        (this->*handlers[type])(hdr);
    }
}

static ssize_t 
read_bytes(int sock, void *buf, size_t count)
{
    ssize_t bytes_read = 0;
    while (bytes_read < (ssize_t)count) {
        int rc = read(sock, (char*)buf + bytes_read, 
                      count - bytes_read);
        if (rc < 0) {
            return rc;
        } else if (rc == 0) {
            return rc;
        }
        bytes_read += rc;
    }
    return bytes_read;
}

void
CSocketReceiver::Run(void)
{
    char name[MAX_NAME_LEN+1];
    memset(name, 0, MAX_NAME_LEN+1);
    snprintf(name, MAX_NAME_LEN, "CSockReceiver %d", csock->osfd);
    set_thread_name(name);

    if (sk->accepting_side) {
        if (csock->send_confirmation() < 0) {
            dbgprintf("CSocketReceiver: failed to send confirmation: %s\n",
                      strerror(errno));
        } // otherwise, we are happily connected.  go forth and read bytes!
    } else {
        if (csock->wait_until_connected() < 0) {
            dbgprintf("CSocketReceiver: csock connect() "
                      "must have failed, exiting\n");
            return;
        } // otherwise, we are happily connected.  go forth and read bytes!
    }

    while (1) {
        struct CMMSocketControlHdr hdr;

        dbgprintf("About to wait for network messages\n");

        int rc = read_bytes(csock->osfd, &hdr, sizeof(hdr));
        if (rc != sizeof(hdr)) {
            dbgprintf("CSocketReceiver: recv failed, rc = %d, errno=%d\n", rc, errno);
            return;
        }

        dbgprintf("Received message: %s\n", hdr.describe().c_str());

        try {
            dispatch(hdr);
        } catch (CMMFatalError& e) {
            dbgprintf("Fatal error on multisocket %d: %s\n",
                      sk->sock, e.what());

            PthreadScopedRWLock lock(&sk->my_lock, false);
            sk->goodbye(false);
            shutdown(sk->select_pipe[1], SHUT_RDWR);
            throw;
        }
    }
}

void
CSocketReceiver::Finish(void)
{
    {
        PthreadScopedLock lock(&sk->scheduling_state_lock);
        csock->csock_recvr = NULL;
        //sk->incoming_irobs.shutdown();
        //csock->remove();
        pthread_cond_broadcast(&sk->scheduling_state_cv);
    }

    dbgprintf("Exiting.\n");

    // nobody will pthread_join to the receiver now, so detach
    //  to make sure the memory gets reclaimed
    detach();

    delete this; // the last thing that will ever be done with this
}

irob_id_t *
CSocketReceiver::read_deps_array(irob_id_t id, int numdeps, 
                                 struct CMMSocketControlHdr& hdr)
{
    irob_id_t *deps = NULL;
    if (numdeps > 0) {
        deps = new irob_id_t[numdeps];
        int datalen = numdeps * sizeof(irob_id_t);
        int rc = read_bytes(csock->osfd, (char*)deps, datalen);
        if (rc != datalen) {
            if (rc < 0) {
                dbgprintf("Error %d on socket %d\n", errno, csock->osfd);
            } else {
                dbgprintf("Expected %d bytes after header, received %d\n", 
                          datalen, rc);
            }
            delete [] deps;

            throw CMMControlException("Socket error", hdr);
        }

        for (int i = 0; i < numdeps; ++i) {
            deps[i] = ntohl(deps[i]);
        }
    }
    return deps;
}

char *
CSocketReceiver::read_data_buffer(irob_id_t id, int datalen,
                                  struct CMMSocketControlHdr& hdr)
{
    if (datalen <= 0) {
        throw CMMControlException("Expected data with header, got none", hdr);
    }
    
    char *buf = new char[datalen];
    int rc = read_bytes(csock->osfd, buf, datalen);
    if (rc != datalen) {
        if (rc < 0) {
            dbgprintf("Error %d on socket %d\n", errno, csock->osfd);
        } else {
            dbgprintf("Expected %d bytes after header, received %d\n", 
                      datalen, rc);
        }
        delete [] buf;
        
        throw CMMControlException("Socket error", hdr);
    }

    dbgprintf("Successfully got %d data bytes\n", datalen);
    return buf;
}

void CSocketReceiver::do_begin_irob(struct CMMSocketControlHdr hdr)
{
    struct timeval begin, end, diff;
    TIME(begin);

    ASSERT(ntohs(hdr.type) == CMM_CONTROL_MSG_BEGIN_IROB);
    irob_id_t id = ntohl(hdr.op.begin_irob.id);
    int numdeps = ntohl(hdr.op.begin_irob.numdeps);
    irob_id_t *deps = NULL;
    try {
        deps = read_deps_array(id, numdeps, hdr);
    } catch (CMMControlException& e) {
        /* Sender sends Data_Check when a network goes down;
         * so this is redundant
        PthreadScopedLock lock(&sk->scheduling_state_lock);

        IROBSchedulingData data(id, CMM_RESEND_REQUEST_DEPS);
        csock->irob_indexes.resend_requests.insert(data);
        pthread_cond_broadcast(&sk->scheduling_state_cv);
        */
        throw;
    }


    PendingReceiverIROB *pirob = new PendingReceiverIROB(id, numdeps, deps, 0, NULL,
                                                         ntohl(hdr.send_labels));
    
    PthreadScopedLock lock(&sk->scheduling_state_lock);
    if (!sk->incoming_irobs.insert(pirob, false)) {
        delete pirob;
        pirob = NULL;
        //throw CMMFatalError("Tried to begin committed IROB", hdr);
        dbgprintf("do_begin_irob: duplicate IROB %ld, ignoring\n", id);
    }

    if (numdeps > 0) {
        delete [] deps;
    }
    
    if (!pirob) {
        return;
    }

    sk->incoming_irobs.release_if_ready(pirob, ReadyIROB());
    
    if (pirob->is_complete() && !pirob->is_placeholder()) {
        schedule_ack(id);
        pthread_cond_broadcast(&sk->scheduling_state_cv);
    }
    
    TIME(end);
    TIMEDIFF(begin, end, diff);
    dbgprintf("Receiver began IROB %ld, took %lu.%06lu seconds\n",
              id, diff.tv_sec, diff.tv_usec);
}

void
CSocketReceiver::schedule_ack(irob_id_t id, bool valid_for_measurement)
{
    // no stored completion time; this must be a duplicate ACK
    // so, let's just say that service time is zero 
    //  (it's pretty close to that anyway; < 1ms, usually)
    struct timeval completion_time = {0, 0};

    if (valid_for_measurement) {
        // now valid; will be used for measurement
        TIME(completion_time);
    }
    IROBSchedulingData data(id, completion_time);
    csock->irob_indexes.waiting_acks.insert(data);
}

void
CSocketReceiver::do_end_irob(struct CMMSocketControlHdr hdr)
{
    struct timeval begin, end, diff;
    TIME(begin);
    ASSERT(ntohs(hdr.type) == CMM_CONTROL_MSG_END_IROB);

    irob_id_t id = ntohl(hdr.op.end_irob.id);
    {
        PthreadScopedLock lock(&sk->scheduling_state_lock);

        bool resend_request = false;
        resend_request_type_t req_type = CMM_RESEND_REQUEST_NONE;

        PendingIROBPtr pirob = sk->incoming_irobs.find(id);
        if (!pirob) {
            if (sk->incoming_irobs.past_irob_exists(id)) {
                //throw CMMFatalError("Tried to end committed IROB", hdr);
                dbgprintf("do_end_irob: previously-finished IROB %ld, "
                          "resending ACK\n", id);

                // can't look up the scheduling time, and it's
                //  probably large if the original ACK was dropped, so
                //  just tell the other end to ignore this IROB for
                //  measurement purposes
                
                // TODO-MEASUREMENT: don't do this for the case when 
                // TODO-MEASUREMENT: I haven't sent the ACK on the network
                // TODO-MEASUREMENT: which delivered the already-finished IROB.
                schedule_ack(id, false);
                pthread_cond_broadcast(&sk->scheduling_state_cv);
                return;
            } else {
                //throw CMMFatalError("Tried to end nonexistent IROB", hdr);
                dbgprintf("Receiver got End_IROB for IROB %ld; "
                          "creating placeholder\n", id);
                PendingIROB *placeholder = sk->incoming_irobs.make_placeholder(id);
                bool ret = sk->incoming_irobs.insert(placeholder, false);
                ASSERT(ret); // since it was absent before now
                pirob = sk->incoming_irobs.find(id);
                
                resend_request = true;
                req_type = resend_request_type_t(CMM_RESEND_REQUEST_DEPS
                                                 | CMM_RESEND_REQUEST_DATA);
            }
        }
        
        ssize_t expected_bytes = ntohl(hdr.op.end_irob.expected_bytes);
        int expected_chunks = ntohl(hdr.op.end_irob.expected_chunks);

        ASSERT(pirob);
        PendingReceiverIROB *prirob = dynamic_cast<PendingReceiverIROB*>(pirob.get());
        if (!prirob->finish(expected_bytes, expected_chunks)) {
            ostringstream s;
            s << "do_end_irob: already-finished IROB " << id;
            if (prirob->is_complete()) {
                dbgprintf("%s, resending ACK\n", s.str().c_str());
            } else {
                dbgprintf("%s, still waiting for deps and/or data\n",
                          s.str().c_str());
                if (!resend_request) {
                    resend_request = true;
                    if (prirob->is_placeholder()) {
                        req_type = resend_request_type_t(req_type |
                                                         CMM_RESEND_REQUEST_DEPS);
                    }
                    if (prirob->recvdbytes() != expected_bytes) {
                        req_type = resend_request_type_t(req_type | 
                                                         CMM_RESEND_REQUEST_DATA);
                    }
                }
            }
        }

        /* Sender sends Data_Check when a network goes down;
         * so this is redundant */
        resend_request = false; // easiest way to disable the resend request

        if (resend_request) {
            IROBSchedulingData data(id, req_type);
            csock->irob_indexes.resend_requests.insert(data);
        }

        sk->incoming_irobs.release_if_ready(prirob, ReadyIROB());

        if (prirob->is_complete() && !prirob->is_placeholder()) {
            schedule_ack(id);
        }

        if (prirob->is_complete() || resend_request) {
            pthread_cond_broadcast(&sk->scheduling_state_cv);
        }
    }

    TIME(end);
    TIMEDIFF(begin, end, diff);
    dbgprintf("Receiver ended IROB %ld, took %lu.%06lu seconds\n",
              id, diff.tv_sec, diff.tv_usec);
}


void CSocketReceiver::do_irob_chunk(struct CMMSocketControlHdr hdr)
{
    struct timeval begin, end, diff;
    TIME(begin);

    ASSERT(ntohs(hdr.type) == CMM_CONTROL_MSG_IROB_CHUNK);
    u_long labels = ntohl(hdr.send_labels);
    irob_id_t id = ntohl(hdr.op.irob_chunk.id);
    int datalen = ntohl(hdr.op.irob_chunk.datalen);
    char *buf = NULL;
    try {
        buf = read_data_buffer(id, datalen, hdr);
    } catch (CMMControlException& e) {
        /* Sender sends Data_Check when a network goes down;
         * so this is redundant
        PthreadScopedLock lock(&sk->scheduling_state_lock);

        IROBSchedulingData data(id, CMM_RESEND_REQUEST_DATA);
        csock->irob_indexes.resend_requests.insert(data);
        pthread_cond_broadcast(&sk->scheduling_state_cv);
        */
        throw;
    }

    hdr.op.irob_chunk.data = buf;

    {
        PthreadScopedLock lock(&sk->scheduling_state_lock);
        csock->update_net_restriction_stats(labels, 0, datalen);
        
        PendingIROBPtr pirob = sk->incoming_irobs.find(id);
        if (!pirob) {
            if (sk->incoming_irobs.past_irob_exists(id)) {
                dbgprintf("do_irob_chunk: duplicate chunk %d for IROB %ld, ignoring\n", 
                          ntohl(hdr.op.irob_chunk.seqno), id);
                delete [] buf;
                return;
            } else {
                dbgprintf("Receiver got IROB_chunk for IROB %ld; "
                          "creating placeholder\n", id);
                PendingIROB *placeholder = sk->incoming_irobs.make_placeholder(id);
                bool ret = sk->incoming_irobs.insert(placeholder, false);
                ASSERT(ret); // since it was absent before now
                pirob = sk->incoming_irobs.find(id);

                /* Sender sends Data_Check when a network goes down;
                 * no recovery needed here, though data may have been dropped */
            }
        }
        struct irob_chunk_data chunk;
        chunk.id = id;
        chunk.seqno = ntohl(hdr.op.irob_chunk.seqno);
        chunk.offset = ntohl(hdr.op.irob_chunk.offset);
        chunk.datalen = ntohl(hdr.op.irob_chunk.datalen);
        chunk.data = hdr.op.irob_chunk.data;
        
        ASSERT(pirob);
        PendingReceiverIROB *prirob = dynamic_cast<PendingReceiverIROB*>(pirob.get());
        ASSERT(prirob);
        if (!prirob->add_chunk(chunk)) {
            delete [] buf;
            if (prirob->is_complete()) {
                dbgprintf("do_irob_chunk: duplicate chunk %lu for IROB %ld, ignoring\n", 
                          chunk.seqno, id);
            } else {
                throw CMMFatalError("Failed to add chunk for IROB; invalid data detected\n", hdr);
            }
        } else {
            dbgprintf("Successfully added chunk %lu to IROB %ld\n",
                      chunk.seqno, id);
        }

        sk->incoming_irobs.release_if_ready(prirob, ReadyIROB());

        if (prirob->is_complete() && !prirob->is_placeholder()) {
            schedule_ack(id);
            pthread_cond_broadcast(&sk->scheduling_state_cv);
        }
    }
    
    TIME(end);
    TIMEDIFF(begin, end, diff);
    dbgprintf("Added chunk %d in IROB %ld, took %lu.%06lu seconds\n",
              ntohl(hdr.op.irob_chunk.seqno), id, diff.tv_sec, diff.tv_usec);
}

void
CSocketReceiver::do_new_interface(struct CMMSocketControlHdr hdr)
{
    ASSERT(ntohs(hdr.type) == CMM_CONTROL_MSG_NEW_INTERFACE);
    struct net_interface iface = hdr.op.new_interface;
    iface.bandwidth_down = ntohl(hdr.op.new_interface.bandwidth_down);
    iface.bandwidth_up = ntohl(hdr.op.new_interface.bandwidth_up);
    iface.RTT = ntohl(hdr.op.new_interface.RTT);
    iface.type = ntohl(hdr.op.new_interface.type);

    sk->setup(iface, false);
}

void
CSocketReceiver::do_down_interface(struct CMMSocketControlHdr hdr)
{
    ASSERT(ntohs(hdr.type) == CMM_CONTROL_MSG_DOWN_INTERFACE);
    struct net_interface iface = {hdr.op.down_interface.ip_addr, 0, 0, 0, 0};
    sk->teardown(iface, false);
}

void
CSocketReceiver::report_ack(irob_id_t id, struct timeval srv_time,
                            struct timeval ack_qdelay, 
                            struct timeval *ack_time)
{
    double bw_out = 0.0, latency_seconds_out = 0.0;
    bool new_measurement = 
        csock->stats.report_ack(id, srv_time, 
                                ack_qdelay, ack_time,
                                &bw_out, &latency_seconds_out);

    u_long bw_est, latency_ms_est;
    bool estimates_valid =
        (csock->stats.get_estimate(NET_STATS_BW_UP, bw_est) &&
         csock->stats.get_estimate(NET_STATS_LATENCY, latency_ms_est));
    
    if (new_measurement && estimates_valid) {
        PthreadScopedLock lock(&sk->scheduling_state_lock);
        
        // XXX: hackish.  Should separate bw and latency updates.
        NetworkChooser *chooser = sk->csock_map->get_network_chooser();
        chooser->reportNetStats(csock->network_type(),
                                bw_out, bw_est,
                                latency_seconds_out,
                                latency_ms_est / 1000.0);
    }
}

void
CSocketReceiver::do_ack(struct CMMSocketControlHdr hdr)
{
    struct timeval begin, end, diff;
    TIME(begin);

    ASSERT(ntohs(hdr.type) == CMM_CONTROL_MSG_ACK);

    irob_id_t id = ntohl(hdr.op.ack.id);
    struct timeval srv_time = {
        (time_t) ntohl(hdr.op.ack.srv_time.tv_sec),
        (suseconds_t) ntohl(hdr.op.ack.srv_time.tv_usec)
    };
    struct timeval ack_qdelay = {
        (time_t) ntohl(hdr.op.ack.qdelay.tv_sec),
        (suseconds_t) ntohl(hdr.op.ack.qdelay.tv_usec)
    };
    struct timeval ack_time;
    TIME(ack_time);
    report_ack(id, srv_time, ack_qdelay, &ack_time);

    sk->ack_received(id);

    size_t num_acks = ntohl(hdr.op.ack.num_acks);
#ifdef CMM_TIMING
    {
        PthreadScopedLock lock(&timing_mutex);
        if (timing_file) {
            struct timeval now;
            TIME(now);
            fprintf(timing_file, "%lu.%06lu : ACKs received for %d IROBs: %ld ", 
                    now.tv_sec, now.tv_usec, num_acks + 1, id);
        }
    }
#endif

    if (num_acks > 0) {
        irob_id_t *acked_irobs = new irob_id_t[num_acks];
        int datalen = num_acks * sizeof(irob_id_t);
        int rc = read_bytes(csock->osfd, (char*)acked_irobs, datalen);
        if (rc != datalen) {
            if (rc < 0) {
                dbgprintf("Error %d on socket %d\n", errno, csock->osfd);
            } else {
                dbgprintf("Expected %d bytes after header, received %d\n", 
                          datalen, rc);
            }
            delete [] acked_irobs;
            throw CMMControlException("Socket error", hdr);
        }

        for (size_t i = 0; i < num_acks; i++) {
            id = ntohl(acked_irobs[i]);
            report_ack(id, srv_time, ack_qdelay, &ack_time);
            sk->ack_received(id);
#ifdef CMM_TIMING
            {
                PthreadScopedLock lock(&timing_mutex);
                if (timing_file) {
                    fprintf(timing_file, "%ld ", id);
                }
            }
#endif
        }
        delete [] acked_irobs;
    }
#ifdef CMM_TIMING
    {
        PthreadScopedLock lock(&timing_mutex);
        if (timing_file) {
            fprintf(timing_file, "\n");
        }
    }
#endif

    TIME(end);
    TIMEDIFF(begin, end, diff);
    dbgprintf("Receiver got ACK for IROB %ld and %u others, took %lu.%06lu seconds\n",
              (irob_id_t)ntohl(hdr.op.ack.id), num_acks,
              diff.tv_sec, diff.tv_usec);
}

void
CSocketReceiver::do_goodbye(struct CMMSocketControlHdr hdr)
{
    ASSERT(ntohs(hdr.type) == CMM_CONTROL_MSG_GOODBYE);
    if (sk->is_shutting_down()) {
        /* I initiated the shutdown; this is the "FIN/ACK" */
        /* at this point, both sides have stopped sending. */
        sk->goodbye_acked();
        throw CMMThreadFinish();
    } else {
        /* The other side initiated the shutdown; this is the "FIN" */
        /* Send the "FIN/ACK" */
        sk->goodbye(true);
        
        /* wake up any selectors */
        dbgprintf("Received goodbye for msocket %d; waking selectors\n",
                  sk->sock);
        shutdown(sk->select_pipe[1], SHUT_RDWR);
    }
    /* Note: the senders will still wait for all waiting ACKs
     * to be sent before finalizing the shutdown. */
}

void
CSocketReceiver::do_request_resend(struct CMMSocketControlHdr hdr)
{
    ASSERT(ntohs(hdr.type) == CMM_CONTROL_MSG_RESEND_REQUEST);
    irob_id_t id = ntohl(hdr.op.resend_request.id);
    resend_request_type_t request = (resend_request_type_t)ntohl(hdr.op.resend_request.request);
    u_long seqno = ntohl(hdr.op.resend_request.seqno);
    //size_t offset = ntohl(hdr.op.resend_request.offset);
    //size_t len = ntohl(hdr.op.resend_request.len);
    int next_chunk = ntohl(hdr.op.resend_request.next_chunk);

    sk->resend_request_received(id, request, seqno,
                                next_chunk);//, offset, len);
}

void
CSocketReceiver::do_data_check(struct CMMSocketControlHdr hdr)
{
    ASSERT(ntohs(hdr.type) == CMM_CONTROL_MSG_DATA_CHECK);
    irob_id_t id = ntohl(hdr.op.data_check.id);
    sk->data_check_requested(id);
}
