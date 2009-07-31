#include "cmm_socket_receiver.h"
#include "cmm_socket_sender.h"
#include <pthread.h>
#include "debug.h"
#include "timeops.h"
#include <vector>
using std::vector;

/* This is where all the scheduling logic happens. 
 * This function decides how to pass IROB data to the application. 
 *
 * Additionally, this function is the only place we need
 * to worry about race conditions with the Receiver thread.
 * Thus, the locking discipline of that thread will be informed
 * by whatever reads and writes are necessary here.
 * 
 * Initial stab at requirements: 
 *   This thread definitely needs to read from the pending_irobs
 *   data structure, to figure out what data to pass to the
 *   application.  It also needs to update this data structure
 *   and the past_irobs set as IROBs are committed.
 * Thought: we could make this function read-only by using the
 *   msg_queue to cause the Receiver thread to do any needed updates.
 */
/* TODO: nonblocking mode */
ssize_t
CMMSocketReceiver::recv(void *bufp, size_t len, int flags, u_long *recv_labels)
{
    vector<PendingReceiverIROB *> pirobs;
    char *buf = (char*)bufp;
    
    struct timeval begin, end, diff;
    TIME(begin);

    ssize_t bytes_ready = 0;
    while ((size_t)bytes_ready < len) {
	struct timeval one_begin, one_end, one_diff;
	TIME(one_begin);
        PendingReceiverIROB *pirob = pending_irobs.get_ready_irob();
	TIME(one_end);
	TIMEDIFF(one_begin, one_end, one_diff);
	dbgprintf("Getting one ready IROB took %lu.%06lu seconds\n",
		  one_diff.tv_sec, one_diff.tv_usec);
	if (!pirob) {
	    if (sk->sendr->is_shutting_down()) {
		return 0;
	    } else {
		assert(0); /* XXX: nonblocking case may return NULL */
	    }
	}

        /* after the IROB is returned here, no other thread will
         * unsafely modify it.
         * XXX: this will not be true if we allow get_next_irob
         * to return released, incomplete IROBs.
         * We could fix that by simply having a sentinel chunk
         * on the concurrent_queue of chunks. */

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
        PendingIROBHash::accessor ac;
        PendingReceiverIROB *pirob = pirobs[i];
        if (!pending_irobs.find(ac, pirob->id)) {
            assert(0);
        }
        assert(pirob == ac->second);
        if (i == 0) {
            if (recv_labels) {
                *recv_labels = pirob->send_labels;
            }
        }

        bytes_passed += pirob->read_data(buf + bytes_passed,
                                         len - bytes_passed);
        if (pirob->is_complete() && pirob->numbytes() == 0) {
            pending_irobs.erase(ac);
            pending_irobs.release_dependents(pirob, ReadyIROB());
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
            pending_irobs.partially_read(pirob);
        }
    }

    TIME(end);
    TIMEDIFF(begin, end, diff);
    dbgprintf("recv: Copying bytes took %lu.%06lu seconds\n",
	      diff.tv_sec, diff.tv_usec);
    dbgprintf("Passing %d bytes to application\n", bytes_passed);
    return bytes_passed;
}
