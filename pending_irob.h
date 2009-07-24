#ifndef pending_irob_h_incl
#define pending_irob_h_incl

#include "tbb/concurrent_queue.h"
#include <set>
#include "libcmm.h"
#include "cmm_socket_control.h"
#include "cmm_socket.private.h"
#include "intset.h"
#include <functional>
#include <string.h>

/* Terminology:
 *  An IROB is _pending_ if the application has not yet received all of its
 *    bytes.
 *  An IROB is _complete_ if all of the data has arrived in our library.
 *  An IROB is _released_ if it is _complete_ AND all of its
 *    dependencies have been satisfied.
 *  Once an IROB has been received in its entirety by the application,
 *    it is no longer pending and this data structure is destroyed.
 */

class PendingIROB {
  public:
    PendingIROB(struct begin_irob_data data,
		u_long send_labels, u_long recv_labels);
    PendingIROB(struct default_irob_data data,
		u_long send_labels, u_long recv_labels);
    virtual ~PendingIROB();

    /* return true on success; false if action is invalid */
    bool add_chunk(struct irob_chunk_data&); /* host byte order */
    bool finish(void);

    void add_dep(irob_id_t id);
    //void add_dependency(PendingIROB *dep);

    void dep_satisfied(irob_id_t id);

    template <typename Predicate>
    void remove_deps_if(Predicate pred);

    ssize_t copy_data(void *buf, size_t len);

    //std::set<PendingIROB *> get_dep_ptrs();
    
    void add_dependent(irob_id_t id);
    
    /* returns true if this IROB directly or transitively 
     * depends on that IROB. */
    bool depends_on(irob_id_t id);

    /* is this IROB "anonymous", depending on all in-flight IROBs? */
    bool is_anonymous(void) const;
    
    /* has all the data arrived? */
    bool is_complete(void) const;

  protected:
    friend class CMMSocketSender;

    /* all integers here are in host byte order */
    irob_id_t id;
    u_long send_labels;
    u_long recv_labels;

    typedef std::set<irob_id_t> irob_id_set;

    /* same here; host byte order */
    irob_id_set deps;

    /* IROBs that depend on me */
    irob_id_set dependents;

    tbb::concurrent_queue<struct irob_chunk_data> chunks;

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
    std::set<irob_id_t>::iterator iter = deps.begin();
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

typedef tbb::concurrent_hash_map
    <irob_id_t, PendingIROB *,
     IntegerHashCompare<irob_id_t> > PendingIROBHash;
    
/* IROBs are related by the depends-on relation.
 * This relation forms a partial-ordering on all IROBs.
 * We think here of the relation pointing upwards, where the 
 * IROBs with no dependencies are at the top of a lattice
 */
class PendingIROBLattice {
  public:
    bool insert(PendingIROBHash::accessor &ac, PendingIROB *pirob);
    bool find(PendingIROBHash::const_accessor &ac, irob_id_t id);
    bool find(PendingIROBHash::accessor &ac, irob_id_t id);
    bool find(PendingIROBHash::const_accessor &ac, PendingIROB *pirob);
    bool find(PendingIROBHash::accessor &ac, PendingIROB *pirob);
    bool erase(irob_id_t id);
    bool erase(PendingIROBHash::accessor &ac);

    bool past_irob_exists(irob_id_t id) const;

    bool any(PendingIROBHash::accessor &ac);

    bool empty() { return pending_irobs.empty(); }
    
    /* returns true if first depends on second. */
    //bool depends_on(PendingIROB *first, PendingIROB *second);

    /* iter_fns are member functions of PendingIROB.  
     * the argument is a given dependent IROB ptr. */
    //typedef void (PendingIROB::*iter_fn_t)(PendingIROB *);
    //void for_each_dep(PendingIROB *dependent, iter_fn_t fn);
  protected:
    PendingIROBHash pending_irobs;

    /* In a sender, this means IROBs that have been sent and ACK'd.
     * In a receiver, this means IROBs that have been received by the app. */
    IntSet past_irobs;

    /* 1) If pirob is anonymous, add deps on all pending IROBs.
     * 2) Otherwise, add deps on all pending anonymous IROBs.
     * 3) Remove already-satisfied deps. */
    void correct_deps(PendingIROB *pirob);

#if 0
    std::map<irob_id_t, struct node *> nodes;
    struct node {
        PendingIROB *pirob;
        irob_id_t id;
        std::set<struct node *> up;
        std::set<struct node *> down;
    };
    struct node bottom;
    struct node top;
#endif
};

#endif
