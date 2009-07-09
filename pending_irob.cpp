#include "pending_irob.h"

PendingIROB::PendingIROB(struct begin_irob_data begin_irob)
    : id(ntohl(begin_irob.id)), 
      send_labels(ntohl(begin_irob.send_labels)), 
      recv_labels(ntohl(begin_irob.recv_labels)),
      anonymous(false), 
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

void PendingIROB::add_dep(irob_id_t id)
{
    deps.insert(id);
}

void 
PendingReceiverIROB::dep_satisfied(irob_id_t id)
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
PendingIROB::add_dependent(irob_id_t id)
{
    dependents.insert(id);
}

bool 
PendingIROB::is_complete(void)
{
    return complete;
}

bool
PendingIROBLattice::insert(PendingIROBHash::accessor& ac, PendingIROB *pirob)
{
    assert(pirob);
    if (past_irobs.contains(pirob->id)) {
        dbgprintf("E: Tried to add irob %d, which I've seen in the past\n", 
                  pirob->id);
        return false;
    }
    if (!pending_irobs.insert(ac, pirob->id)) {
        ac.release();
        dbgprintf("E: Tried to add irob %d, which I've already added\n",
                  pirob->id);
        return false;
    }
    ac->second = pirob;

    correct_deps(pirob);
    return true;
}

void
PendingIROBLattice::correct_deps(PendingIROB *pirob)
{
    /* 1) If pirob is anonymous, add deps on all pending IROBs. */
    /* 2) Otherwise, add deps on all pending anonymous IROBs. */
    for (PendingIROBHash::iterator it = pending_irobs.begin();
         it != pending_irobs.end(); it++) {
        if (it->second == pirob) continue;

        if (pirob->is_anonymous() || it->second->is_anonymous()) {
            pirob->add_dep(it->first);
        }
    }
    /* 3) Remove already-satisfied deps. */
    pirob->remove_deps_if(bind1st(mem_fun_ref(&IntSet::contains), 
                                  past_irobs));
}

bool
PendingIROBLattice::find(PendingIROBHash::const_accessor& ac, 
                         PendingIROB *pirob)
{
    assert(pirob);
    return pending_irobs.find(ac, pirob->id);
}

bool
PendingIROBLattice::find(PendingIROBHash::accessor& ac, PendingIROB *pirob)
{
    assert(pirob);
    return pending_irobs.find(ac, pirob->id);
}

bool
PendingIROBLattice::find(PendingIROBHash::const_accessor& ac, Predicate pred)
{
    for (PendingIROBHash::iterator it = pending_irobs.begin();
         it != pending_irobs.end(); it++) {
        if (!pending_irobs.find(ac, it->first)) {
            assert(0);
        }
        assert(ac->second == it->second);
        if (pred(it->second)) {
            return true;
        }
        ac.release();
    }
    return false;
}

bool
PendingIROBLattice::find(PendingIROBHash::accessor& ac, Predicate pred)
{
    for (PendingIROBHash::iterator it = pending_irobs.begin();
         it != pending_irobs.end(); it++) {
        PendingIROBHash::const_accessor read_ac;
        if (!pending_irobs.find(read_ac, it->first)) {
            assert(0);
        }
        assert(read_ac->second == it->second);
        if (pred(it->second)) {
            read_ac.release();
            if (!pending_irobs.find(ac, it->first)) {
                assert(0);
            }
            return true;
        }
    }
    return false;
}

bool
PendingIROBLattice::erase(irob_id_t id)
{
    return pending_irobs.erase(id);
}

bool
PendingIROBLattice::erase(PendingIROBHash::accessor& ac)
{
    return pending_irobs.erase(ac);
}

bool 
PendingIROBLattice::past_irob_exists(irob_id_t id)
{
    return past_irobs.contains(id);
}

#if 0
void
PendingIROBLattice::add_deps(struct node *pos)
{
    PendingIROB *pirob = pos->pirob;
    for (std::set<irob_id_t>::iterator it = pirob->deps.begin();
         it != pirob->deps.end(); it++) {
        irob_id_t id = *it;
        if (nodes.find(id) != nodes.end()) {
            
        }
    }
}

/* returns true if first depends on second. */
bool 
PendingIROBLattice::depends_on(PendingIROB *first, PendingIROB *second)
{
    
}

void 
PendingIROBLattice::for_each_dep(PendingIROB *dependent, iter_fn_t fn)
{
    
}
#endif
