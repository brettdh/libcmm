#include "pending_irob.h"
#include "pending_sender_irob.h"
#include "debug.h"
#include "timeops.h"
#include <functional>
#include <vector>
#include <deque>
using std::deque;
using std::vector; using std::max;
using std::mem_fun_ref;
using std::bind1st;

#include "pthread_util.h"

static pthread_mutex_t count_mutex = PTHREAD_MUTEX_INITIALIZER;
ssize_t PendingIROB::obj_count = 0;

PendingIROB::PendingIROB(irob_id_t id_)
    : id(id_),
      anonymous(false), 
      complete(false),
      placeholder(true),
      status(0)
{
    /* this placeholder PendingIROB will be replaced by 
       the real one, when it arrives. */
}

PendingIROB::PendingIROB(irob_id_t id_, int numdeps, const irob_id_t *deps_array,
			 size_t datalen, char *data, u_long send_labels_)
    : id(id_),
      send_labels(send_labels_),
      anonymous(false),
      complete(false),
      placeholder(false),
      status(0)
{
    if (numdeps > 0) {
        assert(deps_array);
    }
    for (int i = 0; i < numdeps; i++) {
        deps.insert(deps_array[i]);
    }

    if (datalen > 0) {
        anonymous = true;
        assert(data);
        struct irob_chunk_data chunk;
        chunk.id = id;
        chunk.seqno = INVALID_IROB_SEQNO;
        chunk.datalen = datalen;
        chunk.offset = 0;
        chunk.data = data;

        chunks.push_back(chunk);
        complete = true;
    }

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

    //dbgprintf("PendingIROB %p is being destroyed\n", this);
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

void
PendingIROB::subsume(PendingIROB *other)
{
    dependents.insert(other->dependents.begin(),
                      other->dependents.end());
    for (size_t i = 0; i < other->chunks.size(); ++i) {
        add_chunk(other->chunks[i]);
    }
    other->chunks.clear(); // prevent double-free
    
    complete = other->complete;
}

PendingIROBLattice::PendingIROBLattice()
    : offset(0), last_anon_irob_id(-1), count(0)
{
    pthread_mutex_init(&membership_lock, NULL);
}

PendingIROBLattice::~PendingIROBLattice()
{
    clear(true);
    pthread_mutex_destroy(&membership_lock);
}

bool
PendingIROBLattice::insert(PendingIROB *pirob, bool infer_deps)
{
    PthreadScopedLock lock(&membership_lock);
    return insert_locked(pirob, infer_deps);
}

bool
PendingIROBLattice::insert_locked(PendingIROB *pirob, bool infer_deps)
{
    assert(pirob);
    if (past_irobs.contains(pirob->id)) {
        dbgprintf("Inserting IROB %ld failed; it's in past_irobs\n", pirob->id);
        return false;
    }

    if (pending_irobs.empty()) {
        offset = pirob->id;
    }

    int index = pirob->id - offset;

    if (index >= 0 && index < (int)pending_irobs.size() && 
        (pending_irobs[index] != NULL &&
         !pending_irobs[index]->placeholder)) {
        dbgprintf("Inserting IROB %ld failed; I have it already??\n", pirob->id);
        return false;
    }
    while (index < 0) {
        pending_irobs.push_front(NULL);
        --offset;
        index = pirob->id - offset;
    }
    if ((int)pending_irobs.size() <= index) {
        pending_irobs.resize(index+1, NULL);
    }
    if (pending_irobs[index]) {
        // grab the placeholder's dependents and replace it
        // with the real PendingIROB
        assert(!pirob->placeholder);
        assert(pending_irobs[index]->placeholder);
        assert(pending_irobs[index]->id == pirob->id);
        pirob->subsume(pending_irobs[index]);
        
        delete pending_irobs[index];
        pending_irobs[index] = pirob;
    } else {
        pending_irobs[index] = pirob;
    }

    if (pirob->placeholder) {
        // pirob only exists to hold dependents until
        // its replacement arrives.
        return true;
    }

    correct_deps(pirob, infer_deps);

    dbgprintf("Adding IROB %ld as dependent of: [ ", pirob->id);
    for (irob_id_set::iterator it = pirob->deps.begin();
         it != pirob->deps.end(); it++) {
        if (past_irobs.contains(*it)) {
            dbgprintf_plain("(%ld) ", *it);
            continue;
        }

        PendingIROB *dep = find_locked(*it);
        if (dep) {
            dbgprintf_plain("%ld ", dep->id);
            dep->add_dependent(pirob->id);
        } else {
            dbgprintf_plain("P%ld ", *it);
            dep = make_placeholder(*it);
            bool ret = insert_locked(dep);
            assert(ret);
            dep->add_dependent(pirob->id);
        }
    }
    dbgprintf_plain("]\n");

    ++count;
    return true;
}

PendingIROB *
PendingIROBLattice::make_placeholder(irob_id_t id)
{
    PendingIROB *pirob = new PendingIROB(id);
    return pirob;
}

void
PendingIROBLattice::clear(bool delete_members)
{
    PthreadScopedLock lock(&membership_lock);

    if (delete_members) {
        for (size_t i = 0; i < pending_irobs.size(); i++) {
            delete pending_irobs[i];
        }
    }
    pending_irobs.clear();
    offset = 0;

    min_dominator_set.clear();
    past_irobs.clear();
    last_anon_irob_id = -1;
    count = 0;
}

void
PendingIROBLattice::correct_deps(PendingIROB *pirob, bool infer_deps)
{
    /* 1) If pirob is anonymous, add deps on all pending IROBs. */
    /* 2) Otherwise, add deps on all pending anonymous IROBs. */
    /* we keep around a min-dominator set of IROBs; that is, 
     * the minimal set of IROBs that the next anonymous IROB
     * must depend on. */

    if (!infer_deps) {
        pirob->remove_deps_if(bind1st(mem_fun_ref(&IntSet::contains), 
                                      past_irobs));
    }

    if (infer_deps) {
        // skip this at the receiver. The sender infers and
        //  communicates deps; the receiver enforces them.

        if (pirob->is_anonymous()) {
            if (last_anon_irob_id >= 0 && min_dominator_set.empty()) {
                pirob->add_dep(last_anon_irob_id);
            } else {
                pirob->deps.insert(min_dominator_set.begin(),
                                   min_dominator_set.end());
                min_dominator_set.clear();
            }
            last_anon_irob_id = pirob->id;
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
            
            if (last_anon_irob_id >= 0 && isect.begin() == the_end) {
                pirob->add_dep(last_anon_irob_id);
            } // otherwise, it already depends on something 
            // that depends on the last_anon_irob
            
            for (vector<irob_id_t>::iterator it = isect.begin(); 
                 it != the_end; ++it) {
                min_dominator_set.erase(*it);
            }
            
            min_dominator_set.insert(pirob->id);
        }
    }

    if (!infer_deps) {
        /* 3) Remove already-satisfied deps. */
        pirob->remove_deps_if(bind1st(mem_fun_ref(&IntSet::contains), 
                                      past_irobs));
    }
}

PendingIROB *
PendingIROBLattice::find(irob_id_t id)
{
    PthreadScopedLock lock(&membership_lock);

    return find_locked(id);
}

PendingIROB *
PendingIROBLattice::find_locked(irob_id_t id)
{
    //TimeFunctionBody timer("pending_irobs.find(accessor)");
    int index = id - offset;
    if (index < 0 || index >= (int)pending_irobs.size()) {
        return NULL;
    }
    return pending_irobs[index];
}

bool
PendingIROBLattice::erase(irob_id_t id, bool at_receiver)
{
    PthreadScopedLock lock(&membership_lock);

    //TimeFunctionBody timer("pending_irobs.erase(const_accessor)");
    int index = id - offset;
    if (index < 0 || index >= (int)pending_irobs.size() ||
        pending_irobs[index] == NULL) {
        dbgprintf("WARNING: failed to remove IROB %ld from lattice!\n", id);
        return false;
    }
    past_irobs.insert(id);
    if (at_receiver) {
        min_dominator_set.erase(id);
    }

    PendingIROB *victim = pending_irobs[index];
    pending_irobs[index] = NULL; // caller must free it
    while (!pending_irobs.empty() && pending_irobs[0] == NULL) {
        pending_irobs.pop_front();
        offset++;
    }

    // don't want to do this at the sender; want to preserve
    // dep information until it arrives at the receiver
    if (at_receiver) {
        dbgprintf("Notifying dependents of IROB %ld's release: [ ", id);
        for (irob_id_set::iterator it = victim->dependents.begin();
             it != victim->dependents.end(); it++) {
            
            PendingIROB *dependent = this->find_locked(*it);
            if (dependent == NULL) {
                dbgprintf_plain("(%ld) ", *it);
                continue;
            }
            dbgprintf_plain("%ld ", dependent->id);
            dependent->dep_satisfied(id);
        }
        dbgprintf_plain("]\n");
    }
    
    --count;
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

size_t
PendingIROBLattice::size()
{
    PthreadScopedLock lock(&membership_lock);
    return count; 
}

// must be holding scheduling_state_lock
vector<irob_id_t>
PendingIROBLattice::get_all_ids()
{
    GetIDs obj;
    for_each_by_ref(obj);
    return obj.ids;
}

struct DataCheckIROB {
    void operator()(PendingIROB *pirob) {
        PendingSenderIROB *psirob = dynamic_cast<PendingSenderIROB*>(pirob);
        if (psirob) {
            psirob->request_data_check();
        }
    }
};

// only call on a lattice that only contains
//  PendingSenderIROBs.
void
PendingIROBLattice::data_check_all()
{
    DataCheckIROB obj;
    for_each_by_ref(obj);
}
