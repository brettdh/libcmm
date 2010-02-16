#include <errno.h>
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
      offset(0), num_bytes(datalen), expected_bytes(-1), expected_chunks(-1), 
      recvd_bytes(0), recvd_chunks(0)
{
    partial_chunk.data = NULL;
    partial_chunk.datalen = 0;

    if (datalen > 0) {
        expected_bytes = recvd_bytes = datalen;
        expected_chunks = recvd_chunks = 1;
    }
    assert(datalen == 0 || is_complete());
}

PendingReceiverIROB::PendingReceiverIROB(irob_id_t id_)
    : PendingIROB(id_),
      offset(0), num_bytes(0), expected_bytes(-1), expected_chunks(-1), 
      recvd_bytes(0), recvd_chunks(0)
{
    /* this placeholder PendingReceiverIROB will be replaced by 
       the real one, when it arrives. */
    partial_chunk.data = NULL;
    partial_chunk.datalen = 0;
}

void
PendingReceiverIROB::subsume(PendingIROB *other)
{
    PendingIROB::subsume(other);

    PendingReceiverIROB *prirob = dynamic_cast<PendingReceiverIROB*>(other);
    assert(prirob);

    offset = prirob->offset; // should be zero
    num_bytes = prirob->num_bytes;
    expected_bytes = prirob->expected_bytes;
    expected_chunks = prirob->expected_chunks;
    recvd_bytes = prirob->recvd_bytes;
    recvd_chunks = prirob->recvd_chunks;
    partial_chunk = prirob->partial_chunk; // should be {NULL, 0}
    prirob->partial_chunk.data = NULL; // prevent double free, just in case
}

PendingReceiverIROB::~PendingReceiverIROB()
{
    // XXX: make sure there are no races here
    delete [] partial_chunk.data;
}

void
PendingReceiverIROB::assert_valid()
{
    assert(recvd_bytes >= 0);
    for (size_t i = 1; i < chunks.size(); ++i) {
        assert(chunks[i-1].data == NULL ||
               chunks[i].data == NULL ||
               ((chunks[i-1].offset + chunks[i-1].datalen)
                == chunks[i].offset));
    }
}

bool
PendingReceiverIROB::add_chunk(struct irob_chunk_data& chunk)
{
    if (!is_complete()) {
        // since we don't release bytes until the IROB is complete
        assert(num_bytes == recvd_bytes);

//         struct irob_chunk_data trimmed_chunk = chunk;
//         //trimmed_chunk.data = NULL;
//         if (chunk.offset < (size_t)recvd_bytes) {
//             for (size_t i = 0; i < chunks.size(); ++i) {
//                 size_t this_chunk_end = chunks[i].offset + chunks[i].datalen;
//                 if (this_chunk_end > trimmed_chunk.offset) {
//                     size_t seen_datalen = this_chunk_end - trimmed_chunk.offset;
//                     dbgprintf("Ignoring %zu bytes of already-seen data at offset %zu\n",
//                               seen_datalen, trimmed_chunk.offset);
//                     if (seen_datalen >= trimmed_chunk.datalen) {
//                         // this entire chunk is redundant; ignore it
//                         return true;
//                     } else {
//                         trimmed_chunk.datalen -= seen_datalen;
//                         trimmed_chunk.offset += seen_datalen;
//                     }
//                 }
//             }
//             assert(trimmed_chunk.datalen > 0);
//             if (trimmed_chunk.datalen < chunk.datalen) {
//                 //trimmed_chunk.data = new char[trimmed_chunk.datalen];
//                 char *bufp = chunk.data + (chunk.datalen - trimmed_chunk.datalen);
//                 //memcpy(trimmed_chunk.data, bufp, trimmed_chunk.datalen);

//                 // no need to reallocate; just overwrite the already-seen data,
//                 // moving the good data to the start of the buffer
//                 memmove(chunk.data, bufp, trimmed_chunk.datalen);
//                 //delete [] chunk.data;
//             }
//             // update datalen, offset
//             chunk = trimmed_chunk;
//         }

//         //assert(chunk.offset == (size_t)recvd_bytes); // no holes
//         assert(chunk.offset >= (size_t)recvd_bytes);
//         if (chunk.offset > (size_t)recvd_bytes) {
//             // there's a hole before this chunk.  Ignore the chunk, and
//             // request a resend starting from the bytes we've already seen
//             // (resend done above this layer)
//             dbgprintf("add_chunk: detected hole in IROB %ld before chunk %lu\n",
//                       id, chunk.seqno);
//             return false;
//         }

        u_long seqno = chunk.seqno;
        if (expected_chunks != -1 && seqno >= (u_long)expected_chunks) {
            dbgprintf("add_chunk: received seqno %lu IROB %ld, "
                      "but expected only %d chunks\n",
                      seqno, id, expected_chunks);
            return false;
        }
        if (seqno >= chunks.size()) {
            struct irob_chunk_data empty;
            memset(&empty, 0, sizeof(empty));
            chunks.resize(seqno + 1, empty);
        }

        if (chunks[seqno].data != NULL) {
            dbgprintf("Ignoring already-seen chunk %lu "
                      "(%zu bytes at offset %zu);\n",
                      chunk.seqno, chunk.datalen, chunk.offset);
            return true;
        }
        recvd_chunks++; // only valid because we don't do partial chunks

        chunks[seqno] = chunk;
        num_bytes += chunk.datalen;
        recvd_bytes += chunk.datalen;
	dbgprintf("Added chunk %lu (%d bytes) to IROB %ld new total %d\n", 
		  chunk.seqno, chunk.datalen, id, num_bytes);
        assert_valid();
    } else {
	dbgprintf("Adding chunk %lu (%d bytes) on IROB %ld failed! recvd_bytes=%d, expected_bytes=%d\n",
		  chunk.seqno, chunk.datalen, id, recvd_bytes, expected_bytes);
        return false;
    }

    return true;
}

bool 
PendingReceiverIROB::is_ready(void)
{
    return !placeholder && deps.empty();
}

bool
PendingReceiverIROB::all_chunks_complete()
{
    for (size_t i = 0; i < chunks.size(); ++i) {
        if (chunks[i].data == NULL) {
            return false;
        }
    }
    return true;
}

vector<struct irob_chunk_data>
PendingReceiverIROB::get_missing_chunks()
{
    vector<struct irob_chunk_data> missing_chunks;
    for (size_t i = 0; i < chunks.size(); ++i) {
        if (chunks[i].data == NULL) {
            // for now, just whole chunks can be missing.
            missing_chunks.push_back(chunks[i]);
        }
    }
    return missing_chunks;
}


bool 
PendingReceiverIROB::is_complete(void)
{
    assert(recvd_bytes <= expected_bytes || expected_bytes == -1);
    assert(expected_chunks == -1 || recvd_chunks <= expected_chunks);
    if (expected_bytes != recvd_bytes) {
        dbgprintf("IROB %ld not complete; expected %d bytes, recvd %d so far\n",
                  id, expected_bytes, recvd_bytes);
        return false;
    }
    if (expected_chunks != recvd_chunks) {
        dbgprintf("IROB %ld not complete; expected %d chunks, recvd %d so far\n",
                  id, expected_chunks, recvd_chunks);
        return false;
    }
    return ((expected_bytes == recvd_bytes) && 
            (expected_chunks == recvd_chunks) && 
            all_chunks_complete() && complete);
}

bool
PendingReceiverIROB::finish(ssize_t expected_bytes_, int num_chunks)
{
    if (is_complete()) {
        return false;
    }
    complete = true;

    if (expected_bytes == -1 || expected_chunks == -1) {
        assert(expected_bytes == -1 && expected_chunks == -1);
        expected_bytes = expected_bytes_;
        expected_chunks = num_chunks;
        if (chunks.size() > (size_t)expected_chunks) {
            dbgprintf("Finished IROB %ld with %d chunks expected, "
                      "but it already has seqnos up to %zu\n",
                      id, expected_bytes, chunks.size());
            return false;
        }
    } else {
        // otherwise, we've already set it;
        // we better not be trying to set it to something
        // different
        assert(expected_bytes == expected_bytes_ &&
               expected_chunks == num_chunks);
        return false;
    }

    return true;
}

ssize_t 
PendingReceiverIROB::read_data(void *buf, size_t len)
{
    ssize_t bytes_copied = 0;

    dbgprintf("Attempting to copy %zu bytes from irob %ld, which has %d bytes,\n"
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
    dbgprintf("Copied %d bytes from IROB %ld\n", bytes_copied, id);
    return bytes_copied;
}

ssize_t 
PendingReceiverIROB::numbytes()
{
    return num_bytes;
}

ssize_t 
PendingReceiverIROB::recvdbytes()
{
    return recvd_bytes;
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

PendingIROB *
PendingReceiverIROBLattice::make_placeholder(irob_id_t id)
{
    PendingReceiverIROB *pirob = new PendingReceiverIROB(id);
    return pirob;
}

bool
PendingReceiverIROBLattice::data_is_ready()
{
    PthreadScopedLock lock(&sk->scheduling_state_lock);
    return (partially_read_irob || !ready_irobs.empty());
}

PendingReceiverIROB PendingReceiverIROBLattice::empty_sentinel_irob(-1);

/* REQ: call with scheduling_state_lock held
 *
 * There's a race on partially_read_irob between get_ready_irob and 
 * partially_read below, but they are only called sequentially. 
 *
 * if block_for_data is true and the socket is blocking, we'll wait
 *  for an IROB to be released.  (for implementing MSG_WAITALL)
 * if block_for_data is false and/or the socket is non-blocking,
 *  we'll return immediately if no IROBs are ready. In the case that
 *  the socket is blocking, this allows us to return however many
 *  bytes are ready, as blocking recv should do.
 */
PendingReceiverIROB *
PendingReceiverIROBLattice::get_ready_irob(bool block_for_data)
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
        dbgprintf("get_ready_irob: returning partially-read IROB %ld\n", 
                  pirob->id);
    } else {
	struct timeval begin, end, diff;
	TIME(begin);
        while (pi == NULL) {
#ifndef CMM_UNIT_TESTING
            while (ready_irobs.empty()) {
                assert(sk);
                {
                    if (sk->remote_shutdown && sk->csock_map->empty()) {
                        dbgprintf("get_ready_irob: socket shutting down; "
                                  "returning NULL\n");
                        return NULL;
                    }
                }
                if (!block_for_data || sk->is_non_blocking()) {
                    dbgprintf("get_ready_irob: none ready and %s; "
                              "I'm returning NULL\n",
                              sk->is_non_blocking()
                              ? "socket is non-blocking"
                              : "bytes previously returned");
                    return &empty_sentinel_irob;
                }
                pthread_cond_wait(&sk->scheduling_state_cv, &sk->scheduling_state_lock);
            }
#endif

            if (!pop_item(ready_irobs, ready_irob_id)) {
                assert(0);
            }
            pi = find(ready_irob_id);
            if (!pi) {
                dbgprintf("Looks like IROB %ld was already received; "
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

        dbgprintf("get_ready_irob: returning IROB %ld\n", 
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
        // if the socket is in blocking mode, this will block
        //    if bytes_passed == 0
        bool block_for_data = ((bytes_passed == 0) || (flags & MSG_WAITALL));
        PendingReceiverIROB *pirob = get_ready_irob(block_for_data);
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
	    if (sk->shutting_down) {
                return bytes_passed;
            } else {
	        assert(0); /* XXX: nonblocking case may return NULL */
            }
#endif
	}

#ifndef CMM_UNIT_TESTING
        if (pirob->numbytes() == 0) {
            // sentinel; no more bytes are ready
            assert(pirob == &empty_sentinel_irob);
            if (bytes_passed == 0) {
                if (!sk->is_non_blocking()) {
                    // impossible; get_ready_irob would have blocked
                    //  until there was data ready to return
                    assert(0);
                }

                bytes_passed = -1;
                errno = EWOULDBLOCK;
            }
            break;
        }
#endif

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
            erase(pirob->id, true);
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
#if defined(CMM_TIMING) && !defined(CMM_UNIT_TESTING)
    if (bytes_passed > 0) {
        PthreadScopedLock lock(&timing_mutex);
        if (timing_file) {
            struct timeval now;
            TIME(now);
            fprintf(timing_file, "[%lu.%06lu] %d bytes received with label %lu in %lu.%06lu seconds\n", 
                    now.tv_sec, now.tv_usec, bytes_passed, timing_recv_labels,
                    diff.tv_sec, diff.tv_usec);
        }
        //global_stats.bytes_received[timing_recv_labels] += bytes_passed;
        //global_stats.recv_count[timing_recv_labels]++;;
    }
#endif
    return bytes_passed;
}
