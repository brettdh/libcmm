#include "pending_irob.h"

PendingIROB::PendingIROB(struct begin_irob_data begin_irob,
                         CMMSocketReceiver *recvr_ = NULL)
    : id(ntohl(begin_irob.id)), 
      send_labels(begin_irob.send_labels), 
      recv_labels(begin_irob.recv_labels),
      recvr(recvr_),
      anonymous(begin_irob.numdeps == -1),
      complete(false)
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

bool
PendingIROB::add_chunk(struct irob_chunk_data irob_chunk)
{
    if (is_complete() || is_released()) {
        return false;
    }
    chunks.push(irob_chunk);
    return true;
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


bool 
PendingIROB::is_complete(void)
{
    return complete;
}

bool 
PendingIROB::is_released(void)
{
    return is_complete() && deps.empty();
}
