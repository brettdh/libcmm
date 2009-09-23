#include "pending_irob.h"
#include "pending_receiver_irob.h"
#include "debug.h"
#include "timeops.h"
#include "cmm_timing.h"
#include "cmm_socket.private.h"
#include "csocket_mapping.h"
#include "pthread_util.h"
#include <algorithm>
#include <vector>
using std::min; using std::vector;

PendingReceiverIROB::PendingReceiverIROB(irob_id_t id, int numdeps, irob_id_t *deps,
                                         size_t datalen, char *data,
					 u_long send_labels)
    : PendingIROB(id, numdeps, deps, datalen, data, send_labels),
      offset(0), num_bytes(datalen)
{
    partial_chunk.data = NULL;
    partial_chunk.datalen = 0;
}

PendingReceiverIROB::~PendingReceiverIROB()
{
    // XXX: make sure there are no races here
    delete [] partial_chunk.data;
}

bool
PendingReceiverIROB::add_chunk(struct irob_chunk_data& chunk)
{
    bool result = PendingIROB::add_chunk(chunk);
    if (result) {
        num_bytes += chunk.datalen;
	dbgprintf("Added chunk %d (%d bytes) to IROB %d\n", 
		  chunk.seqno, chunk.datalen, id);
    } else {
	dbgprintf("Adding chunk %d (%d bytes) on IROB %d failed!\n",
		  chunk.seqno, chunk.datalen, id);
    }
    return result;
}

bool 
PendingReceiverIROB::is_ready(void)
{
    return !placeholder && deps.empty();
}

ssize_t 
PendingReceiverIROB::read_data(void *buf, size_t len)
{
    ssize_t bytes_copied = 0;

    dbgprintf("Attempting to copy %lu bytes from irob %d, which has %d bytes,\n"
	      "                   %d untouched chunks, and %s partial chunk\n", 
	      len, id, num_bytes, chunks.size(), (partial_chunk.data?"a":"no"));
    if (partial_chunk.data) {
	dbgprintf("Copying first from partial chunk; offset=%d, datalen=%d\n",
		  offset, partial_chunk.datalen);
        ssize_t bytes = min(len, partial_chunk.datalen - offset);
        assert(bytes > 0);
        memcpy(buf, partial_chunk.data + offset, bytes);
        if (len >= (partial_chunk.datalen - offset)) {
            delete [] partial_chunk.data;
            partial_chunk.data = NULL;
            partial_chunk.datalen = 0;
            offset = 0;
        } else {
            offset += bytes;
        }
        len -= bytes;
        bytes_copied += bytes;
    }

    /* if len == 0 here, we'll skip the loop */
    while (len > 0 && !chunks.empty()) {
        struct irob_chunk_data chunk = chunks.front();
        chunks.pop_front();

	dbgprintf("Copying from chunk: datalen=%d, data=%p\n", 
		  chunk.datalen, chunk.data);

        ssize_t bytes = min(len, chunk.datalen);
        memcpy((char*)buf + bytes_copied, chunk.data, bytes);
        bytes_copied += bytes;
        if (chunk.datalen > len) {
            offset = bytes;
            partial_chunk = chunk;
        } else {
            delete [] chunk.data;
        }
        len -= bytes;
	dbgprintf("Read %d bytes; %d bytes remaining in request\n",
		  bytes, len);
    }
    num_bytes -= bytes_copied;
    dbgprintf("Copied %d bytes from IROB %d\n", bytes_copied, id);
    return bytes_copied;
}

ssize_t 
PendingReceiverIROB::numbytes()
{
    return num_bytes;
}

PendingReceiverIROBLattice::PendingReceiverIROBLattice(CMMSocketImpl *sk_)
    : sk(sk_), partially_read_irob(NULL)
{
}

PendingReceiverIROBLattice::~PendingReceiverIROBLattice()
{
    // XXX: make sure there are no races here
    delete partially_read_irob;
}

bool
PendingReceiverIROBLattice::data_is_ready()
{
    PthreadScopedLock lock(&sk->scheduling_state_lock);
    return (partially_read_irob || !ready_irobs.empty());
}

/* REQ: call with scheduling_state_lock held
 *
 * There's a race on partially_read_irob between get_ready_irob and 
 * partially_read below, but they are only called sequentially. */
PendingReceiverIROB *
PendingReceiverIROBLattice::get_ready_irob()
{
    irob_id_t ready_irob_id = -1;
    PendingIROB *pi = NULL;
    PendingReceiverIROB *pirob = NULL;
    if (partially_read_irob) {
        if (!partially_read_irob->is_complete()) {
            /* TODO: block until more bytes are available */
	    assert(0);
        }
        pirob = partially_read_irob;
        partially_read_irob = NULL;
        dbgprintf("get_ready_irob: returning partially-read IROB %d\n", 
                  pirob->id);
    } else {
        /* TODO: nonblocking */
	struct timeval begin, end, diff;
	TIME(begin);
        while (pi == NULL) {
#ifndef CMM_UNIT_TESTING
            while (ready_irobs.empty()) {
                assert(sk);
                {
                    PthreadScopedLock lock(&sk->shutdown_mutex);
                    if (sk->remote_shutdown && sk->csock_map->empty()) {
                        dbgprintf("get_ready_irob: returning NULL\n");
                        return NULL;
                    }
                }
                pthread_cond_wait(&sk->scheduling_state_cv, &sk->scheduling_state_lock);
            }
#endif

            if (!pop_item(ready_irobs, ready_irob_id)) {
                assert(0);
            }
            pi = find(ready_irob_id);
            if (!pi) {
                dbgprintf("Looks like IROB %d was already received; "
                          "ignoring\n", ready_irob_id);
            }
	}
	TIME(end);
	TIMEDIFF(begin, end, diff);
	dbgprintf("recv: spent %lu.%06lu seconds waiting for a ready IROB\n",
		  diff.tv_sec, diff.tv_usec);

        assert(pi);
        pirob = dynamic_cast<PendingReceiverIROB*>(pi);
        assert(pirob);

        dbgprintf("get_ready_irob: returning IROB %d\n", 
                  pirob->id);
    }
    return pirob;
}

// must call with scheduling_state_lock held
void
PendingReceiverIROBLattice::release(irob_id_t id)
{
    if (ready_irobs.empty()) {
#ifndef CMM_UNIT_TESTING
        dbgprintf("waking selectors for msocket %d\n",
                  sk->sock);
        char c = 42; // value will be ignored
        (void)send(sk->select_pipe[1], &c, 1, MSG_NOSIGNAL);
#endif
    }
    ready_irobs.insert(id);
#ifndef CMM_UNIT_TESTING
    pthread_cond_broadcast(&sk->scheduling_state_cv);
#endif
}

/* This is where all the scheduling logic happens. 
 * This function decides how to pass IROB data to the application. 
 */
/* TODO: nonblocking mode */
ssize_t
PendingReceiverIROBLattice::recv(void *bufp, size_t len, int flags,
                                 u_long *recv_labels)
{
    vector<PendingReceiverIROB *> pirobs;
    char *buf = (char*)bufp;
    
#ifdef CMM_TIMING
    u_long timing_recv_labels = 0;
#endif

    struct timeval begin, end, diff;
    TIME(begin);

#ifndef CMM_UNIT_TESTING
    PthreadScopedLock lock(&sk->scheduling_state_lock);
    // will be released while waiting for bytes to arrive
#endif

    ssize_t bytes_passed = 0;
    while ((size_t)bytes_passed < len) {
	struct timeval one_begin, one_end, one_diff;
	TIME(one_begin);
        PendingReceiverIROB *pirob = get_ready_irob();
	TIME(one_end);
	TIMEDIFF(one_begin, one_end, one_diff);
	dbgprintf("Getting one ready IROB took %lu.%06lu seconds\n",
		  one_diff.tv_sec, one_diff.tv_usec);
	if (!pirob) {
            // XXX: for now assume we're shutting down; we need a sentinel
            // to differentiate this from non-blocking read with no ready data
            
#ifdef CMM_UNIT_TESTING
            return bytes_passed;
#else
            assert(sk);
	    if (sk->is_shutting_down()) {
                return bytes_passed;
            } else {
	        assert(0); /* XXX: nonblocking case may return NULL */
            }
#endif
	}

        /* after the IROB is returned here, no other thread will
         * unsafely modify it.
         * XXX: this will not be true if we allow get_next_irob
         * to return ready, incomplete IROBs.
         * We could fix that by simply having a sentinel chunk
         * on the queue of chunks. */

        assert(pirob->is_ready());
        assert(pirob->is_complete()); /* XXX: see get_next_irob */

        if (bytes_passed == 0) {
            if (recv_labels) {
                *recv_labels = pirob->send_labels;
            }
#ifdef CMM_TIMING
            timing_recv_labels = pirob->send_labels;
#endif
        }

        bytes_passed += pirob->read_data(buf + bytes_passed,
                                         len - bytes_passed);
        if (pirob->is_complete() && pirob->numbytes() == 0) {
            erase(pirob->id);
            release_dependents(pirob, ReadyIROB());
            delete pirob;
        } else {
            assert(partially_read_irob == NULL);
            partially_read_irob = pirob;
        }
    }
    TIME(end);
    TIMEDIFF(begin, end, diff);
    dbgprintf("recv: gathering and copying bytes took %lu.%06lu seconds\n", 
	      diff.tv_sec, diff.tv_usec);

    dbgprintf("Passing %d bytes to application\n", bytes_passed);
#ifndef CMM_UNIT_TESTING
#ifdef CMM_TIMING
    if (bytes_passed > 0) {
        PthreadScopedLock lock(&timing_mutex);
        if (timing_file) {
            struct timeval now;
            TIME(now);
            fprintf(timing_file, "[%lu.%06lu] %d bytes received with label %lu in %lu.%06lu seconds\n\n", 
                    now.tv_sec, now.tv_usec, bytes_passed, timing_recv_labels,
                    diff.tv_sec, diff.tv_usec);
        }
        //global_stats.bytes_received[timing_recv_labels] += bytes_passed;
        //global_stats.recv_count[timing_recv_labels]++;;
    }
#endif
#endif
    return bytes_passed;
}
