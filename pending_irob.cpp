#include "pending_irob.h"
#include "debug.h"
#include "timeops.h"
#include <functional>
#include <vector>
using std::vector; using std::max;
using std::mem_fun_ref;
using std::bind1st;

static pthread_mutex_t count_mutex = PTHREAD_MUTEX_INITIALIZER;
ssize_t PendingIROB::obj_count = 0;

PendingIROB::PendingIROB(irob_id_t id_, int numdeps, const irob_id_t *deps_array,
			 u_long send_labels_, u_long recv_labels_)
    : id(id_),
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

    PthreadScopedLock lock(&count_mutex);
    ++obj_count;
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

    PthreadScopedLock lock(&count_mutex);
    ++obj_count;
}

PendingIROB::~PendingIROB()
{
    while (!chunks.empty()) {
        struct irob_chunk_data chunk = chunks.front();
        chunks.pop_front();
        delete [] chunk.data;
    }

    PthreadScopedLock lock(&count_mutex);
    --obj_count;
}

ssize_t
PendingIROB::objs()
{
    PthreadScopedLock lock(&count_mutex);
    return obj_count;
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
PendingIROB::depends_on(irob_id_t that)
{
    return (deps.count(that) == 1);
}


PendingIROBLattice::PendingIROBLattice()
    : offset(0), last_anon_irob(NULL)
{
    pthread_mutex_init(&membership_lock, NULL);
}

PendingIROBLattice::~PendingIROBLattice()
{
    PthreadScopedLock lock(&membership_lock);
    for (size_t i = 0; i < pending_irobs.size(); i++) {
        delete pending_irobs[i];
    }
}

bool
PendingIROBLattice::insert(PendingIROB *pirob)
{
    PthreadScopedLock lock(&membership_lock);

    assert(pirob);
    if (past_irobs.contains(pirob->id)) {
        return false;
    }

    //TimeFunctionBody timer("pending_irobs.insert");

    size_t index = pirob->id - offset;
    if (index < pending_irobs.size() && pending_irobs[index] != NULL) {
        return false;
    }
    if (pending_irobs.size() <= index) {
        pending_irobs.resize(index+1, NULL);
    }
    pending_irobs[index] = pirob;

    correct_deps(pirob);

    for (irob_id_set::iterator it = pirob->deps.begin();
         it != pirob->deps.end(); it++) {
        PendingIROB *dep = find(*it);
        if (dep) {
            dep->add_dependent(pirob->id);
        }
    }

    return true;
}

void
PendingIROBLattice::clear()
{
    PthreadScopedLock lock(&membership_lock);

    pending_irobs.clear();
    min_dominator_set.clear();
    last_anon_irob = NULL;
}

void
PendingIROBLattice::correct_deps(PendingIROB *pirob)
{
    /* 1) If pirob is anonymous, add deps on all pending IROBs. */
    /* 2) Otherwise, add deps on all pending anonymous IROBs. */
    /* we keep around a min-dominator set of IROBs; that is, 
     * the minimal set of IROBs that the next anonymous IROB
     * must depend on. */

    pirob->remove_deps_if(bind1st(mem_fun_ref(&IntSet::contains), 
                                  past_irobs));

    if (pirob->is_anonymous()) {
        if (last_anon_irob && min_dominator_set.empty()) {
            pirob->add_dep(last_anon_irob->id);
        } else {
            pirob->deps.insert(min_dominator_set.begin(),
                               min_dominator_set.end());
            min_dominator_set.clear();
        }
        last_anon_irob = pirob;
    } else {
        // this IROB dominates its deps, so remove them from
        // the min_dominator_set before inserting it
        vector<irob_id_t> isect(max(pirob->deps.size(), 
                                    min_dominator_set.size()));
        vector<irob_id_t>::iterator the_end = 
            set_intersection(pirob->deps.begin(), pirob->deps.end(),
                             min_dominator_set.begin(), 
                             min_dominator_set.end(),
                             isect.begin());
        
        if (last_anon_irob && isect.begin() == the_end) {
            pirob->add_dep(last_anon_irob->id);
        } // otherwise, it already depends on something 
        // that depends on the last_anon_irob

        for (vector<irob_id_t>::iterator it = isect.begin(); 
             it != the_end; ++it) {
            min_dominator_set.erase(*it);
        }
        
        min_dominator_set.insert(pirob->id);
    }

#if 0
    for (size_t i = 0; i < pending_irobs.size(); i++) {
	if (pending_irobs[i] == pirob) continue;
	
	if (pending_irobs[i] &&
            (pirob->is_anonymous() || pending_irobs[i]->is_anonymous())) {
	    pirob->add_dep(i);
	}
    }
#endif

    /* 3) Remove already-satisfied deps. */
    pirob->remove_deps_if(bind1st(mem_fun_ref(&IntSet::contains), 
                                  past_irobs));
}

PendingIROB *
PendingIROBLattice::find(irob_id_t id)
{
    PthreadScopedLock lock(&membership_lock);

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
    PthreadScopedLock lock(&membership_lock);

    //TimeFunctionBody timer("pending_irobs.erase(const_accessor)");
    size_t index = id - offset;
    if (index < 0 || index >= pending_irobs.size() ||
        pending_irobs[index] == NULL) {
        return false;
    }
    past_irobs.insert(id);
    min_dominator_set.erase(id);
    if (last_anon_irob && last_anon_irob->id == id) {
        last_anon_irob = NULL;
    }
    PendingIROB *victim = pending_irobs[index];
    pending_irobs[index] = NULL; // caller must free it
    while (!pending_irobs.empty() && pending_irobs[0] == NULL) {
        pending_irobs.pop_front();
        offset++;
    }

    for (irob_id_set::iterator it = victim->dependents.begin();
         it != victim->dependents.end(); it++) {
        
        PendingIROB *dependent = this->find(*it);
        if (dependent == NULL) {
            continue;
        }
        dependent->dep_satisfied(id);
    }

    return true;
}

bool 
PendingIROBLattice::past_irob_exists(irob_id_t id)
{
    PthreadScopedLock lock(&membership_lock);
    return past_irobs.contains(id);
}

bool 
PendingIROBLattice::empty()
{
    PthreadScopedLock lock(&membership_lock);
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
