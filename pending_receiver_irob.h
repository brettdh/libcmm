#ifndef pending_irob_h_incl
#define pending_irob_h_incl

#include <queue>
#include <set>
#include "libcmm.h"
#include "cmm_socket_control.h"
#include "cmm_socket.private.h"
#include "intset.h"

typedef std::unary_function<irob_id_t, bool> Predicate;

class PendingReceiverIROB : public PendingIROB {
  public:
    PendingIROB(struct begin_irob_data data, 
                CMMSocketReceiver *recvr_);
    
    /* return true on success; false if action is invalid */
    bool add_chunk(struct irob_chunk_data&);
    bool finish(void);
    
    void dep_satisfied(irob_id_t id);
    void remove_deps_if(Predicate pred);

    void add_dependent(irob_id_t id);
    void release_dependents();

    /* is this IROB "anonymous", depending on all in-flight IROBs? */
    bool is_anonymous(void);
    
    /* has all the data arrived? */
    bool is_complete(void);

    /* have all the deps been satisfied? 
     * (only meaningful on the receiver side) */
    bool is_released(void);

  private:
    friend class CMMSocketSender;
    friend class CMMSocketReceiver;
    
    /* all integers here are in host byte order */
    irob_id_t id;
    u_long send_labels;
    u_long recv_labels;
    CMMSocketReceiver *recvr;

    std::set<irob_id_t> deps;
    std::queue<struct irob_chunk_data> chunks;
    ssize_t bytes_read;

    bool anonymous;
    bool complete;
};

#endif
