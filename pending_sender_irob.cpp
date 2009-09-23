#include "pending_irob.h"
#include "pending_sender_irob.h"
#include "debug.h"

PendingSenderIROB::PendingSenderIROB(irob_id_t id_, 
                                     int numdeps, const irob_id_t *deps_array,
                                     size_t datalen, char *data,
				     u_long send_labels,
                                     resume_handler_t resume_handler_, 
                                     void *rh_arg_)
    : PendingIROB(id_, numdeps, deps_array, datalen, data, send_labels),
      next_seqno(INVALID_IROB_SEQNO + 1),
      resume_handler(resume_handler_), rh_arg(rh_arg_),
      acked(false),
      waiting_thread(pthread_self())
{
}

bool
PendingSenderIROB::add_chunk(struct irob_chunk_data& irob_chunk)
{
    if (is_complete()) {
        return false;
    }

    irob_chunk.seqno = next_seqno++;
    chunks.push_back(irob_chunk);
    //waiting_threads[irob_chunk.seqno] = pthread_self();

    return true;
}

void
PendingSenderIROB::ack(u_long seqno)
{
    if (seqno >= next_seqno) {
        dbgprintf("Invalid seqno %lu for ack in IROB %lu\n", seqno, id);
        throw CMMException();
    }

    if (seqno == INVALID_IROB_SEQNO) {
        acked = true;
    } else {
        acked_chunks.insert(seqno);
        if (is_complete() && acked_chunks.size() == (size_t)chunks.size()) {
            acked = true;
        }
    }
}

bool
PendingSenderIROB::is_acked(void)
{
    return acked;
}
