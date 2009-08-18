#include "pending_irob.h"
#include "pending_receiver_irob.h"
#include "debug.h"
#include "timeops.h"
#include <algorithm>
#include <vector>
using std::min; using std::vector;

PendingReceiverIROB::PendingReceiverIROB(struct begin_irob_data begin_irob,
					 u_long send_labels, u_long recv_labels)
    : PendingIROB(begin_irob.id, begin_irob.numdeps, begin_irob.deps,
                  send_labels, recv_labels), offset(0), num_bytes(0)
{
    partial_chunk.data = NULL;
    partial_chunk.datalen = 0;
}

PendingReceiverIROB::PendingReceiverIROB(struct default_irob_data default_irob,
					 u_long send_labels, u_long recv_labels)
    : PendingIROB(default_irob.id, default_irob.datalen, default_irob.data,
                  send_labels, recv_labels), offset(0),
      num_bytes(default_irob.datalen)
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
PendingReceiverIROB::is_released(void)
{
    return deps.empty();
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

PendingReceiverIROBLattice::PendingReceiverIROBLattice()
    : partially_read_irob(NULL)
{
    pthread_mutex_init(&ready_mutex, NULL);
    pthread_cond_init(&ready_cv, NULL);
}

PendingReceiverIROBLattice::~PendingReceiverIROBLattice()
{
    // XXX: make sure there are no races here
    delete partially_read_irob;
}

/* There's a race on partially_read_irob between get_ready_irob and 
 * partially_read below, but they are only called sequentially. */
PendingReceiverIROB *
PendingReceiverIROBLattice::get_ready_irob()
{
    PendingReceiverIROB *pirob = NULL;
    if (partially_read_irob) {
        if (!partially_read_irob->is_complete()) {
            /* TODO: block until more bytes are available */
	    assert(0);
        }
        pirob = partially_read_irob;
        partially_read_irob = NULL;
    } else {
        /* TODO: nonblocking */
	struct timeval begin, end, diff;
	TIME(begin);
	pthread_mutex_lock(&ready_mutex);
        while (ready_irobs.empty()) {
	    pthread_cond_wait(&ready_cv, &ready_mutex);
	}
	pirob = ready_irobs.front();
	ready_irobs.pop();
	pthread_mutex_unlock(&ready_mutex);
	TIME(end);
	TIMEDIFF(begin, end, diff);
	dbgprintf("recv: spent %lu.%06lu seconds blocked on the IROB queue\n",
		  diff.tv_sec, diff.tv_usec);
    }
    return pirob;
}

void
PendingReceiverIROBLattice::partially_read(PendingReceiverIROB *pirob)
{
    assert(partially_read_irob == NULL);
    partially_read_irob = pirob;
}

void
PendingReceiverIROBLattice::shutdown()
{
    /* This will cause further recvs to return 0 (EOF). */
    enqueue(NULL);
}

void
PendingReceiverIROBLattice::enqueue(PendingReceiverIROB *pirob)
{
    pthread_mutex_lock(&ready_mutex);
    ready_irobs.push(pirob);
    pthread_cond_signal(&ready_cv);
    pthread_mutex_unlock(&ready_mutex);
}

/* This is where all the scheduling logic happens. 
 * This function decides how to pass IROB data to the application. 
 *
 * REQ: call with scheduling_state_lock held
 */
/* TODO: nonblocking mode */
ssize_t
PendingReceiverIROBLattice::recv(void *bufp, size_t len, int flags,
                                 u_long *recv_labels)
{
    vector<PendingReceiverIROB *> pirobs;
    char *buf = (char*)bufp;
    
    struct timeval begin, end, diff;
    TIME(begin);

    ssize_t bytes_ready = 0;
    while ((size_t)bytes_ready < len) {
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
            
	    //if (sk->is_shutting_down()) {
            return 0;
            //} else {
	        //assert(0); /* XXX: nonblocking case may return NULL */
            //}
	}

        /* after the IROB is returned here, no other thread will
         * unsafely modify it.
         * XXX: this will not be true if we allow get_next_irob
         * to return released, incomplete IROBs.
         * We could fix that by simply having a sentinel chunk
         * on the queue of chunks. */

        assert(pirob->is_released());
        assert(pirob->is_complete()); /* XXX: see get_next_irob */

        ssize_t bytes = pirob->numbytes();
        assert(bytes > 0);
        bytes_ready += bytes;

        pirobs.push_back(pirob);

        if (!pirob->is_complete()) {
            break;
        }
    }
    TIME(end);
    TIMEDIFF(begin, end, diff);
    dbgprintf("recv: gathering bytes took %lu.%06lu seconds\n", 
	      diff.tv_sec, diff.tv_usec);

    TIME(begin);

    ssize_t bytes_passed = 0;
    bool partial_irob = false;
    for (size_t i = 0; i < pirobs.size(); i++) {
        PendingReceiverIROB *pirob = pirobs[i];
        if (i == 0) {
            if (recv_labels) {
                *recv_labels = pirob->send_labels;
            }
        }

        bytes_passed += pirob->read_data(buf + bytes_passed,
                                         len - bytes_passed);
        if (pirob->is_complete() && pirob->numbytes() == 0) {
            erase(pirob->id);
            release_dependents(pirob, ReadyIROB());
            delete pirob;
        } else {
            if (!pirob->is_complete()) {
                /* This should still be the last one in the list,
                 * since it MUST finish before any IROB can be
                 * passed to the application. */
            }
            /* this should be true for at most the last IROB
             * in the vector */
            assert(!partial_irob);
            partial_irob = true;
            partially_read(pirob);
        }
    }

    TIME(end);
    TIMEDIFF(begin, end, diff);
    dbgprintf("recv: Copying bytes took %lu.%06lu seconds\n",
	      diff.tv_sec, diff.tv_usec);
    dbgprintf("Passing %d bytes to application\n", bytes_passed);
    return bytes_passed;
}
