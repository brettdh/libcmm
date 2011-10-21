#ifndef pending_irob_h_incl
#define pending_irob_h_incl

#include <vector>
#include <deque>
#include "libcmm.h"
#include "cmm_socket_control.h"
//#include "cmm_socket.private.h"
#include "intset.h"
#include <functional>
#include <string.h>
#include <boost/shared_ptr.hpp>

#include "pthread_util.h"

/* Terminology:
 *  An IROB is _pending_ if the application has not yet received all of its
 *    bytes.
 *  An IROB is _complete_ if all of the data has arrived in our library.
 *  An IROB is _ready_ if it is _complete_ AND all of its
 *    dependencies have been satisfied.
 *  Once an IROB has been received in its entirety by the application,
 *    it is no longer pending and this data structure is destroyed.
 */

struct ResumeOperation;

typedef std::set<irob_id_t> irob_id_set;

class PendingIROB;
typedef boost::shared_ptr<PendingIROB> PendingIROBPtr;

class PendingIROB {
  public:
    PendingIROB(irob_id_t id_, int numdeps, const irob_id_t *deps_array,
                size_t datalen, char *data, u_long send_labels);
    virtual ~PendingIROB();

    /* return true on success; false if action is invalid */
    virtual bool add_chunk(struct irob_chunk_data&); /* host byte order */

    void add_dep(irob_id_t id);
    //void add_dependency(PendingIROB *dep);

    void dep_satisfied(irob_id_t id);

    template <typename Predicate>
    void remove_deps_if(Predicate pred);

    // stores the dependencies of this IROB into a new[]-allocated array
    //  and stores the start of that array at *deps.
    // returns the number of deps stored into the array.
    //  if this returns 0, *deps was not modified.
    // since this function is used for formatting network messages,
    //  the integer IDs are stored in network byte order.
    size_t copy_deps_htonl(irob_id_t **deps);

    void add_dependent(irob_id_t id);
    
    /* returns true if this IROB directly
     * depends on that IROB. */
    bool depends_on(irob_id_t id);

    /* is this IROB "anonymous", depending on all in-flight IROBs? */
    bool is_anonymous(void) const;
    
    /* has all the data arrived? */
    virtual bool is_complete(void) const;

    // number of PendingIROBs in existence
    static ssize_t objs();

  protected:
    bool finish(void);

    static ssize_t obj_count;

    friend class CMMSocketImpl;
    friend class CSocketSender;
    friend class CSocketReceiver;

    friend void resume_operation_thunk(ResumeOperation *op);
    
    /* all integers here are in host byte order */
    irob_id_t id;
    u_long send_labels;

    /* same here; host byte order */
    irob_id_set deps;

    /* IROBs that depend on me */
    irob_id_set dependents;

/*     std::deque<struct irob_chunk_data, */
/*                boost::pool_allocator<struct irob_chunk_data> > chunks; */
    std::deque<struct irob_chunk_data> chunks;

    bool anonymous;
    bool complete;
    bool placeholder;


    // 0 if there have been no csocket_sender errors with this IROB.
    // If there is no suitable network for this IROB and I haven't
    // sent all of its data, status will be:
    //    CMM_DEFERRED if I registered the IROB's thunk.
    //    CMM_FAILED if there's no thunk.
    // In the no-thunk case, I might currently be blocking, or 
    //  I might've timed out.  The csocket_sender thread and
    //  the application thread that's blocking coordinate this.
    int status;

    friend class PendingIROBLattice;

    // Copy metadata from other and take ownership of its data chunks.
    // other should be a placeholder.
    virtual void subsume(PendingIROB *other);


    /* if not NULL, points to the lattice that this IROB belongs to. 
     * Used for keeping track of the domset of IROBs, which is needed
     * for figuring out which IROBs anonymous IROBs directly depend on. */
    //PendingIROBLattice *lattice;
  protected:
    PendingIROB(irob_id_t id); // for creating placeholder IROBs
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
    // XXX: these default args are really describing what happens at the sender.
    //  maybe these should be moved into a PendingSenderIROBLattice.
    // TODO: yes, they should.  Do it!
    bool insert(PendingIROB *pirob, bool infer_deps = true);
    PendingIROBPtr find(irob_id_t id);
    bool erase(irob_id_t id, bool at_receiver = false);

    // only call at the sender.
    void drop_irob_and_dependents(irob_id_t id);

    bool past_irob_exists(irob_id_t id);

    bool empty();// { return pending_irobs.empty(); }
    size_t size();
    void clear();
    
    /* returns true if first depends on second. */
    //bool depends_on(PendingIROB *first, PendingIROB *second);

    /* iter_fns are member functions of PendingIROB.  
     * the argument is a given dependent IROB ptr. */
    //typedef void (PendingIROB::*iter_fn_t)(PendingIROB *);
    //void for_each_dep(PendingIROB *dependent, iter_fn_t fn);

    // returns a placeholder IROB of the correct subtype for this lattice
    virtual PendingIROB *make_placeholder(irob_id_t id);

    // must be holding sk->scheuling_state_lock
    template <typename Functor>
    void for_each_by_ref(Functor& f);

    std::vector<irob_id_t> get_all_ids();
    
  private:
    // must hold membership_lock
    bool insert_locked(PendingIROB *pirob, bool infer_deps = true);
    PendingIROBPtr find_locked(irob_id_t id);
    bool erase_locked(irob_id_t id, bool at_receiver = false);
    
    // Invariant: pending_irobs.empty() || 
    //            (pending_irobs[0] != NULL &&
    //             forall_i>0(pending_irobs[i] == NULL || 
    //                        pending_irobs[i]->id == i + offset))
    std::deque<PendingIROBPtr> pending_irobs;
    size_t offset;
    pthread_mutex_t membership_lock;

    // the last anonymous IROB added to the set, if any.
    // -1 otherwise.
    irob_id_t last_anon_irob_id;

    // number of real (not placeholder) IROBs in the data structure.
    size_t count;
    
    // If a new IROB depended upon all IROBs in this set, it would
    //  transitively depend on all prior IROBs.
    // This set contains no anonymous IROB IDs; see last_anon_irob_id.
    irob_id_set min_dominator_set;

    /* In a sender, this means IROBs that have been sent and ACK'd.
     * In a receiver, this means IROBs that have been received by the app. */
    IntSet past_irobs;

    /* 1) If pirob is anonymous, add deps on all pending IROBs.
     * 2) Otherwise, add deps on all pending anonymous IROBs.
     * 3) Remove already-satisfied deps. */
    void correct_deps(PendingIROBPtr pirob, bool infer_deps);

    struct GetIDs {
        std::vector<irob_id_t> ids;
        void operator()(PendingIROBPtr pirob) {
            ids.push_back(pirob->id);
        }
    };
};

template <typename Functor>
void PendingIROBLattice::for_each_by_ref(Functor& f)
{
    PthreadScopedLock lock(&membership_lock);
    for (size_t i = 0; i < pending_irobs.size(); ++i) {
        if (pending_irobs[i]) {
            f(pending_irobs[i]);
        }
    }
}

#endif
