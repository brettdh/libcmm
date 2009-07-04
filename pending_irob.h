#ifndef pending_irob_h_incl
#define pending_irob_h_incl

#include <queue>
#include <set>
#include "libcmm.h"
#include "cmm_socket_control.h"
#include "cmm_socket.private.h"
#include "intset.h"

/* Terminology:
 *  An IROB is _pending_ if the application has not yet received all of its
 *    bytes.
 *  An IROB is _complete_ if all of the data has arrived in our library.
 *  An IROB is _released_ if it is _complete_ AND all of its
 *    dependencies have been satisfied.
 *  Once an IROB has been received in its entirety by the application,
 *    it is no longer pending and this data structure is destroyed.
 */

typedef std::unary_function<irob_id_t, bool> Predicate;

class PendingIROB {
  public:
    PendingIROB(mc_socket_t, struct begin_irob_data, CMMSocketReceiver *);
    
    /* return true on success; false if action is invalid */
    bool add_chunk(struct irob_chunk_data&);
    bool finish(void);
    
    void add_dep(irob_id_t id);
    void dep_satisfied(irob_id_t id);

    void ack(u_long seqno = INVALID_IROB_SEQNO);

    void remove_deps_if(Predicate pred);

    /* is this IROB "anonymous", depending on all in-flight IROBs? */
    bool is_anonymous(void);
    
    /* has all the data arrived? */
    bool is_complete(void);

    /* is it complete, and have all the deps been satisfied? 
     * (only meaningful on the receiver side) */
    bool is_released(void);

    /* is it complete, and 
     * have all the chunks been acked, or has the IROB been acked? */
    bool is_acked(void);
  private:
    friend class CMMSocketSender;
    friend class CMMSocketReceiver;
    
    /* all integers here are in host byte order */
    irob_id_t id;
    u_long next_seqno;
    u_long send_labels;
    u_long recv_labels;
    resume_handler_t resume_handler;
    void *rh_arg;
    CMMSocketReceiver *recvr;

    std::set<irob_id_t> deps;
    std::queue<struct irob_chunk_data> chunks;

    IntSet acked_chunks;
    
    bool anonymous;
    bool complete;
    bool acked;
};

#endif
