#include "pending_irob.h"
#include "pending_sender_irob.h"

PendingSenderIROB::PendingSenderIROB(struct begin_irob_data begin_irob,
                         resume_handler_t resume_handler_, void *rh_arg_)
    : PendingIROB(begin_irob),
      resume_handler(resume_handler_), rh_arg(rh_arg_),
      complete(false),
      acked(false),
      next_seqno(INVALID_IROB_SEQNO + 1)
{
    int numdeps = ntohl(begin_irob.numdeps);
    if (numdeps > 0) {
        assert(begin_irob.deps);
    } else if (numdeps == -1) {
        anonymous = true;
    }
    for (int i = 0; i < numdeps; i++) {
        deps.insert(ntohl(begin_irob.deps[i]));
    }
}

PendingSenderIROB::~PendingSenderIROB()
{
    while (!chunks.empty()) {
        struct irob_chunk_data chunk;
        chunks.pop(chunk);
        delete [] chunk.data;
    }
}

bool
PendingSenderIROB::add_chunk(struct irob_chunk_data& irob_chunk)
{
    if (!PendingIROB::add_chunk(irob_chunk)) {
        return false;
    }
    irob_chunk.seqno = next_seqno++;
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
        if (is_complete() && acked_chunks.size() == chunks.size()) {
            acked = true;
        }
    }
}

bool
PendingSenderIROB::is_acked(void)
{
    return acked;
}

bool
PendingSenderIROB::finish(void)
{
    if (is_complete()) {
        return false;
    }
    complete = true;
    return true;
}

bool 
PendingSenderIROB::is_complete(void)
{
    return complete;
}
