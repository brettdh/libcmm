#include "pending_irob.h"
#include "debug.h"
#include <functional>
using std::mem_fun_ref;
using std::bind1st;

PendingIROB::PendingIROB(struct begin_irob_data begin_irob,
			 u_long send_labels_, u_long recv_labels_)
    : id(ntohl(begin_irob.id)),
      send_labels(send_labels_),
      recv_labels(recv_labels_),
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

PendingIROB::PendingIROB(struct default_irob_data default_irob,
			 u_long send_labels_, u_long recv_labels_)
    : id(ntohl(default_irob.id)), 
      send_labels(send_labels_), 
      recv_labels(recv_labels_),
      anonymous(true), 
      complete(true)
{
    struct irob_chunk_data chunk;
    chunk.id = id;
    chunk.seqno = INVALID_IROB_SEQNO;
    chunk.datalen = ntohl(default_irob.datalen);
    chunk.data = default_irob.data;
    chunks.push(chunk);
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
    if (is_complete()) {
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
PendingIROB::add_dep(irob_id_t id)
{
    deps.insert(id);
}

void 
PendingIROB::dep_satisfied(irob_id_t id)
{
    deps.erase(id);
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
PendingIROB::is_anonymous(void)
{
    return anonymous;
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
    dbgprintf("Begin: pending_irobs.insert\n");
    if (!pending_irobs.insert(ac, pirob->id)) {
        ac.release();
        dbgprintf("E: Tried to add irob %d, which I've already added\n",
                  pirob->id);
        return false;
    }
    ac->second = pirob;

    correct_deps(pirob);
    dbgprintf("End: pending_irobs.insert\n");
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
                         irob_id_t id)
{
    dbgprintf("Begin: pending_irobs.find(const_accessor)\n");
    bool result = pending_irobs.find(ac, id);
    dbgprintf("End: pending_irobs.find(const_accessor)\n");
    return result;
}

bool
PendingIROBLattice::find(PendingIROBHash::accessor& ac, 
                         irob_id_t id)
{
    dbgprintf("Begin: pending_irobs.find(accessor)\n");
    bool result = pending_irobs.find(ac, id);
    dbgprintf("End: pending_irobs.find(accessor)\n");
    return result;
}

bool
PendingIROBLattice::find(PendingIROBHash::const_accessor& ac, 
                         PendingIROB *pirob)
{
    assert(pirob);
    dbgprintf("Begin: pending_irobs.find(const_accessor)\n");
    bool result = pending_irobs.find(ac, pirob->id);
    dbgprintf("End: pending_irobs.find(const_accessor)\n");
    return result;
}

bool
PendingIROBLattice::find(PendingIROBHash::accessor& ac, PendingIROB *pirob)
{
    assert(pirob);
    dbgprintf("Begin: pending_irobs.find(accessor)\n");
    bool result = pending_irobs.find(ac, pirob->id);
    dbgprintf("End: pending_irobs.find(accessor)\n");
    return result;
}

bool 
PendingIROBLattice::any(PendingIROBHash::accessor &ac)
{
    if (pending_irobs.empty()) {
        return false;
    } else {
        return find(ac, pending_irobs.begin()->first);
    }
}

bool
PendingIROBLattice::erase(irob_id_t id)
{
    dbgprintf("Begin: pending_irobs.erase(const_accessor)\n");
    bool result = pending_irobs.erase(id);
    dbgprintf("End: pending_irobs.erase(const_accessor)\n");
    return result;
}

bool
PendingIROBLattice::erase(PendingIROBHash::accessor& ac)
{
    dbgprintf("Begin: pending_irobs.erase(accessor)\n");
    bool result = pending_irobs.erase(ac);
    dbgprintf("End: pending_irobs.erase(accessor)\n");
    return result;
}

bool 
PendingIROBLattice::past_irob_exists(irob_id_t id) const
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
