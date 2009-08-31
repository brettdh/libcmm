#ifndef pending_sender_irob_h_incl
#define pending_sender_irob_h_incl

#include <queue>
#include <set>
#include "libcmm.h"
#include "cmm_socket_control.h"
//#include "cmm_socket.private.h"
#include "intset.h"
#include "pending_irob.h"
#include <map>

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
    PendingSenderIROB(irob_id_t id_, int numdeps, const irob_id_t *deps_array,
		      u_long send_labels, 
		      resume_handler_t resume_handler, void *rh_arg);
    PendingSenderIROB(irob_id_t id_, size_t datalen, char *data,
		      u_long send_labels, 
                      resume_handler_t resume_handler, void *rh_arg);

    virtual bool add_chunk(struct irob_chunk_data&);
    
    void ack(u_long seqno = INVALID_IROB_SEQNO);

    /* is it complete, and 
     * have all the chunks been acked, or has the IROB been acked? */
    bool is_acked(void);

  private:
    friend class CMMSocketImpl;
    friend class CSocketSender;

    /* all integers here are in host byte order */
    u_long next_seqno;

    resume_handler_t resume_handler;
    void *rh_arg;

    std::map<u_long, pthread_t> waiting_threads;

    IntSet acked_chunks;
    
    bool acked;

    pthread_t waiting_thread;
};

#endif
