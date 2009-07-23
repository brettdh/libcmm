#ifndef pending_sender_irob_h_incl
#define pending_sender_irob_h_incl

#include <queue>
#include <set>
#include "libcmm.h"
#include "cmm_socket_control.h"
#include "cmm_socket.private.h"
#include "intset.h"
#include "pending_irob.h"

/* Terminology:
 *  An IROB is _pending_ if the application has not yet received all of its
 *    bytes.
 *  An IROB is _complete_ if all of the data has arrived in our library.
 *  An IROB is _released_ if it is _complete_ AND all of its
 *    dependencies have been satisfied.
 *  Once an IROB has been received in its entirety by the application,
 *    it is no longer pending and this data structure is destroyed.
 */

class PendingSenderIROB : public PendingIROB {
  public:
    PendingSenderIROB(struct begin_irob_data data,
                      resume_handler_t resume_handler, void *rh_arg);
    PendingSenderIROB(struct default_irob_data data,
                      resume_handler_t resume_handler, void *rh_arg);
    
    bool add_chunk(struct irob_chunk_data&);
    
    void ack(u_long seqno = INVALID_IROB_SEQNO);

    /* is it complete, and 
     * have all the chunks been acked, or has the IROB been acked? */
    bool is_acked(void);

  private:
    friend class CMMSocketSender;
    
    /* all integers here are in host byte order */
    u_long next_seqno;

    resume_handler_t resume_handler;
    void *rh_arg;

    IntSet acked_chunks;
    
    bool acked;
};

#endif
