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
 *  Once the PendingSenderIROB has been ACK'd by the remote receiver,
 *    it is no longer pending at the sender and this data structure is destroyed.
 */

class PendingSenderIROB : public PendingIROB {
  public:
    PendingSenderIROB(irob_id_t id_, int numdeps, const irob_id_t *deps_array,
                      size_t datalen, char *data,
		      u_long send_labels, 
                      resume_handler_t resume_handler, void *rh_arg);

    virtual bool add_chunk(struct irob_chunk_data&);

    /* Gathers up to bytes_requested bytes from this IROB,
     * taking into account the bytes that have already been sent. 
     * Successive calls to this function will return the same data,
     * unless calls to mark_sent() are interleaved. 
     * The sender should operate like this:
     *    data_to_send = psirob->get_ready_bytes(chunksize);
     *    rc = send(data_to_send)
     *    if (rc == success) {
     *        psirob->mark_sent(chunksize);
     *    }
     *    ...
     *
     * and so on.  If bytes_requested is <= 0, the
     * next partially or wholly unsent chunk (as given by the
     * application) will be returned, whatever its size.
     *
     * Side effect: bytes_requested is modified to reflect the
     * actual number of bytes returned.
     */
    std::vector<struct iovec> get_ready_bytes(ssize_t& bytes_requested, 
                                              u_long& seqno) const;

    /* Marks the next bytes_sent bytes as sent.  This in essence advances the
     * pointer that get_ready_bytes provides into this IROB's data. 
     * If the modified bytes_requested parameter from get_ready_bytes is passed
     * to mark_sent, it should never exceed the number of remaining bytes
     * in the IROB. */
    void mark_sent(ssize_t bytes_sent);

    void ack();

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

    bool announced;
    bool end_announced;
    bool acked;

    // for sending partial chunks
    u_long next_seqno_to_send;
    size_t next_chunk;
    size_t chunk_offset;
};

#endif
