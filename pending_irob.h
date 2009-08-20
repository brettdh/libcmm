#ifndef pending_irob_h_incl
#define pending_irob_h_incl

#include "tbb/concurrent_queue.h"
#include <deque>
#include "libcmm.h"
#include "cmm_socket_control.h"
//#include "cmm_socket.private.h"
#include "intset.h"
#include <functional>
#include <string.h>
#include <boost/pool/pool_alloc.hpp>

/* Terminology:
 *  An IROB is _pending_ if the application has not yet received all of its
 *    bytes.
 *  An IROB is _complete_ if all of the data has arrived in our library.
 *  An IROB is _released_ if it is _complete_ AND all of its
 *    dependencies have been satisfied.
 *  Once an IROB has been received in its entirety by the application,
 *    it is no longer pending and this data structure is destroyed.
 */

struct ResumeOperation;

/* typedef std::set<irob_id_t, std::less<irob_id_t>, */
/*                  boost::fast_pool_allocator<irob_id_t> > irob_id_set; */

typedef std::set<irob_id_t> irob_id_set;

class PendingIROB {
  public:
    PendingIROB(irob_id_t id_, int numdeps, const irob_id_t *deps_array,
		u_long send_labels, u_long recv_labels);
    PendingIROB(irob_id_t id_, size_t datalen, char *data,
		u_long send_labels, u_long recv_labels);
    virtual ~PendingIROB();

    /* return true on success; false if action is invalid */
    virtual bool add_chunk(struct irob_chunk_data&); /* host byte order */
    bool finish(void);

    void add_dep(irob_id_t id);
    //void add_dependency(PendingIROB *dep);

    void dep_satisfied(irob_id_t id);

    template <typename Predicate>
    void remove_deps_if(Predicate pred);

    void add_dependent(irob_id_t id);
    
    /* returns true if this IROB directly
     * depends on that IROB. */
    bool depends_on(irob_id_t id);

    /* is this IROB "anonymous", depending on all in-flight IROBs? */
    bool is_anonymous(void) const;
    
    /* has all the data arrived? */
    bool is_complete(void) const;

    // number of PendingIROBs in existence
    static ssize_t objs();
  protected:
    static ssize_t obj_count;

    friend class CMMSocketImpl;
    friend class CSocketSender;
    friend class CSocketReceiver;

    friend void resume_operation_thunk(ResumeOperation *op);
    
    /* all integers here are in host byte order */
    irob_id_t id;
    u_long send_labels;
    u_long recv_labels;

    /* same here; host byte order */
    irob_id_set deps;

    /* IROBs that depend on me */
    irob_id_set dependents;

/*     std::deque<struct irob_chunk_data, */
/*                boost::pool_allocator<struct irob_chunk_data> > chunks; */
    std::deque<struct irob_chunk_data> chunks;

    bool anonymous;
    bool complete;

    friend class PendingIROBLattice;

    /* if not NULL, points to the lattice that this IROB belongs to. 
     * Used for keeping track of the domset of IROBs, which is needed
     * for figuring out which IROBs anonymous IROBs directly depend on. */
    //PendingIROBLattice *lattice;
};

template <typename Predicate>
void 
PendingIROB::remove_deps_if(Predicate pred)
{
    irob_id_set::iterator iter = deps.begin();
    while (iter != deps.end()) {
        irob_id_t id = *iter;
        if (pred(id)) {
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

/* IROBs are related by the depends-on relation.
 * This relation forms a partial-ordering on all IROBs.
 * We think here of the relation pointing upwards, where the 
 * IROBs with no dependencies are at the top of a lattice
 */
class PendingIROBLattice {
  public:
    PendingIROBLattice();
    virtual ~PendingIROBLattice();
    bool insert(PendingIROB *pirob);
    PendingIROB * find(irob_id_t id);
    bool erase(irob_id_t id);

    bool past_irob_exists(irob_id_t id);

    bool empty();// { return pending_irobs.empty(); }
    void clear();
    
    /* returns true if first depends on second. */
    //bool depends_on(PendingIROB *first, PendingIROB *second);

    /* iter_fns are member functions of PendingIROB.  
     * the argument is a given dependent IROB ptr. */
    //typedef void (PendingIROB::*iter_fn_t)(PendingIROB *);
    //void for_each_dep(PendingIROB *dependent, iter_fn_t fn);

  private:
    // Invariant: pending_irobs.empty() || 
    //            (pending_irobs[0] != NULL &&
    //             forall_i>0(pending_irobs[i] == NULL || 
    //                        pending_irobs[i]->id == i + offset))
    std::deque<PendingIROB *> pending_irobs;
    size_t offset;
    pthread_mutex_t membership_lock;

    // the last anonymous IROB added to the set, if any, and
    //  if it hasn't been erased.
    // NULL otherwise.
    PendingIROB *last_anon_irob;
    
    // If a new IROB depended upon all IROBs in this set, it would
    //  transitively depend on all prior IROBs.
    // This set contains no anonymous IROB IDs; see last_anon_irob.
    irob_id_set min_dominator_set;

    /* In a sender, this means IROBs that have been sent and ACK'd.
     * In a receiver, this means IROBs that have been received by the app. */
    IntSet past_irobs;

    /* 1) If pirob is anonymous, add deps on all pending IROBs.
     * 2) Otherwise, add deps on all pending anonymous IROBs.
     * 3) Remove already-satisfied deps. */
    void correct_deps(PendingIROB *pirob);

};

struct IROBSchedulingData {
    IROBSchedulingData(irob_id_t id=-1, u_long seqno=INVALID_IROB_SEQNO);
    bool operator<(const IROBSchedulingData& other) const;

    irob_id_t id;
    u_long seqno; // may be INVALID_IROB_SEQNO
    //u_long send_labels;
    //u_long recv_labels;
    // add scheduling hints later
};

struct IROBSchedulingIndexes {
    std::set<IROBSchedulingData> new_irobs;
    std::set<IROBSchedulingData> new_chunks;
    std::set<IROBSchedulingData> finished_irobs;

    std::set<IROBSchedulingData> waiting_acks;
};

#endif
