#include "pending_irob.h"
#include "debug.h"
#include "timeops.h"
#include <functional>
using std::mem_fun_ref;
using std::bind1st;

PendingIROB::PendingIROB(irob_id_t id_, int numdeps, const irob_id_t *deps_array,
			 u_long send_labels_, u_long recv_labels_)
    : id(id),
      send_labels(send_labels_),
      recv_labels(recv_labels_),
      anonymous(false),
      complete(false)
{
    if (numdeps > 0) {
        assert(deps_array);
    } else if (numdeps == -1) {
        anonymous = true;
    }
    for (int i = 0; i < numdeps; i++) {
        deps.insert(deps_array[i]);
    }
}

PendingIROB::PendingIROB(irob_id_t id_, size_t datalen, char *data,
			 u_long send_labels_, u_long recv_labels_)
    : id(id_), 
      send_labels(send_labels_), 
      recv_labels(recv_labels_),
      anonymous(true), 
      complete(false)
{
    struct irob_chunk_data chunk;
    chunk.id = id;
    chunk.seqno = INVALID_IROB_SEQNO;
    chunk.datalen = datalen;
    chunk.data = data;
    
    (void)add_chunk(chunk);
    (void)finish();
}

PendingIROB::~PendingIROB()
{
    while (!chunks.empty()) {
        struct irob_chunk_data chunk = chunks.front();
        chunks.pop_front();
        delete [] chunk.data;
    }
}

bool
PendingIROB::add_chunk(struct irob_chunk_data& irob_chunk)
{
    if (is_complete()) {
        return false;
    }

    chunks.push_back(irob_chunk);
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
PendingIROB::is_complete(void) const
{
    return complete;
}

bool
PendingIROB::is_anonymous(void) const
{
    return anonymous;
}

bool
PendingIROBLattice::insert(PendingIROB *pirob)
{
    assert(pirob);
    if (past_irobs.contains(pirob->id)) {
        dbgprintf("E: Tried to add irob %d, which I've seen in the past\n", 
                  pirob->id);
        return false;
    }

    //TimeFunctionBody timer("pending_irobs.insert");

    size_t index = pirob->id - offset;
    if (index < pending_irobs.size() && pending_irobs[index] != NULL) {
        dbgprintf("E: Tried to add irob %d, which I've already added\n",
                  pirob->id);
        return false;
    }
    pending_irobs[index] = pirob;

    correct_deps(pirob);

    return true;
}

void
PendingIROBLattice::correct_deps(PendingIROB *pirob)
{
    /* 1) If pirob is anonymous, add deps on all pending IROBs. */
    /* 2) Otherwise, add deps on all pending anonymous IROBs. */
    for (size_t i = 0; i < pending_irobs.size(); i++) {
	if (pending_irobs[i] == pirob) continue;
	
	if (pending_irobs[i] &&
            (pirob->is_anonymous() || pending_irobs[i]->is_anonymous())) {
	    pirob->add_dep(i);
	}
    }

    /* 3) Remove already-satisfied deps. */
    pirob->remove_deps_if(bind1st(mem_fun_ref(&IntSet::contains), 
                                  past_irobs));
}

PendingIROB *
PendingIROBLattice::find(irob_id_t id)
{
    //TimeFunctionBody timer("pending_irobs.find(accessor)");
    size_t index = id - offset;
    if (index < 0 || index >= pending_irobs.size()) {
        return NULL;
    }
    return pending_irobs[index];
}

bool
PendingIROBLattice::erase(irob_id_t id)
{
    //TimeFunctionBody timer("pending_irobs.erase(const_accessor)");
    size_t index = id - offset;
    if (index < 0 || index >= pending_irobs.size()) {
        return false;
    }
    pending_irobs[index] = NULL; // caller must free it
    while (!pending_irobs.empty() && pending_irobs[0] == NULL) {
        pending_irobs.pop_front();
        offset++;
    }
    return true;
}

bool 
PendingIROBLattice::past_irob_exists(irob_id_t id) const
{
    return past_irobs.contains(id);
}

bool 
PendingIROBLattice::empty()
{
    return pending_irobs.empty(); 
}

IROBSchedulingData::IROBSchedulingData(irob_id_t id_, u_long seqno_) 
    : id(id_), seqno(seqno_) 
{
    /* empty */
}

bool 
IROBSchedulingData::operator<(const IROBSchedulingData& other) const
{
    // can implement priority here, based on 
    //  any added scheduling hints
    return (id < other.id) || (seqno < other.seqno);
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
