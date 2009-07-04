#include "pending_irob.h"

PendingIROB::PendingIROB(struct begin_irob_data begin_irob,
                         resume_handler_t resume_handler_, void *rh_arg_,
                         CMMSocketReceiver *recvr_)
    : id(ntohl(begin_irob.id)), 
      send_labels(begin_irob.send_labels), 
      recv_labels(begin_irob.recv_labels),
      resume_handler(resume_handler_), rh_arg(rh_arg_),
      recvr(recvr_),
      anonymous(begin_irob.numdeps == -1),
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

    if (recvr) {
        recvr->correct_deps(this);
    }
}

PendingIROB::~PendingIROB()
{
    while (!chunks.empty()) {
        struct irob_chunk_data chunk;
        chunks.pop(chunk);
        delete [] chunk.data;
    }
}

bool
PendingIROB::add_chunk(struct irob_chunk_data& irob_chunk)
{
    if (is_complete() || is_released()) {
        return false;
    }
    irob_chunk.seqno = next_seqno++;
    chunks.push(irob_chunk);
    return true;
}

void
PendingIROB::ack(u_long seqno)
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
PendingIROB::is_acked(void)
{
    return acked;
}

bool
PendingIROB::finish(void)
{
    if (is_complete()) {
        return false;
    }
    complete = true;
    return true;
}

void 
PendingIROB::dep_satisfied(irob_id_t id)
{
    deps.erase(id);
}

void 
PendingIROB::remove_deps_if(Predicate pred)
{
    std::set<irob_id_t>::iterator iter = deps.begin();
    while (iter != deps.end()) {
        if (pred(*iter)) {
            /* subtle; erases, but doesn't invalidate the 
             * post-advanced iterator
             * see http://stackoverflow.com/questions/800955/
             */
            deps.erase(iter++);
        } else {
            ++iter;
        }
    }
}

void
PendingIROB::add_dependent(PendingIROB *dependent)
{
    
}

void
PendingIROB::release_dependents()
{
    
}

bool 
PendingIROB::is_complete(void)
{
    return complete;
}

bool 
PendingIROB::is_released(void)
{
    return deps.empty();
}
