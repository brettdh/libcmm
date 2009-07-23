#include "pending_irob.h"
#include "pending_sender_irob.h"
#include "debug.h"

PendingSenderIROB::PendingSenderIROB(struct begin_irob_data begin_irob,
                                     resume_handler_t resume_handler_, 
                                     void *rh_arg_)
    : PendingIROB(begin_irob),
      next_seqno(INVALID_IROB_SEQNO + 1),
      resume_handler(resume_handler_), rh_arg(rh_arg_),
      acked(false)
{
}

PendingSenderIROB::PendingSenderIROB(struct default_irob_data data,
				     resume_handler_t resume_handler_, void *rh_arg_)
    : PendingIROB(data),
      next_seqno(INVALID_IROB_SEQNO + 1),
      resume_handler(resume_handler_), rh_arg(rh_arg_),
      acked(false)
{
}


bool
PendingSenderIROB::add_chunk(struct irob_chunk_data& irob_chunk)
{
    irob_chunk.seqno = next_seqno;
    if (!PendingIROB::add_chunk(irob_chunk)) {
        return false;
    }
    next_seqno++;
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
