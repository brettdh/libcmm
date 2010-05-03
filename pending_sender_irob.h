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
     * Successive calls to this function will return different data,
     * unless calls to bytes_not_received() are interleaved. 
     * The sender should operate like this:
     *    data_to_send = psirob->get_ready_bytes(chunksize);
     *    rc = send(data_to_send)
     *    if (rc != success) {
     *        psirob->bytes_not_received(chunksize);
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
                                              u_long& seqno,
                                              size_t& offset);

    /* Marks the next bytes_sent bytes as sent.  This in essence advances the
     * pointer that get_ready_bytes provides into this IROB's data. 
     * If the modified bytes_requested parameter from get_ready_bytes is passed
     * to mark_sent, it should never exceed the number of remaining bytes
     * in the IROB. */
    // purpose of this is now rolled into get_ready_bytes.
    //void mark_sent(ssize_t bytes_sent);

    // Request that the sender ask the receiver if any data is missing.
    void request_data_check();

    // Returns true iff the sender should ask the receiver if any
    //  data is missing.
    bool needs_data_check();

    /* "Rewinds" the get_ready_bytes pointer to the beginning of this IROB,
     *   plus offset bytes.
     * This is needed when a Resend_Request is received for the IROB's data.
     */
    //void rewind(size_t offset_);

    size_t expected_bytes();  // number of bytes given by the application.
    bool all_chunks_sent(); // true if all bytes have been put into send-chunks.

    // Mark these bytes as not received; they will be resent.
    //  The chunk data in missing_chunk may identify fewer bytes
    //  than are in the original chunk (e.g. because a partial chunk
    //  was received)
    // JKLOL: no partial chunks for now.
    void mark_not_received(u_long seqno);//, size_t offset, size_t len);

    // Mark chunks not received from next_chunk to the end.
    void mark_drop_point(int next_chunk);

    void ack();

    /* is it complete, and 
     * have all the chunks been acked, or has the IROB been acked? */
    bool is_acked(void);

  private:
    friend class CMMSocketImpl;
    friend class CSocketSender;
    friend class PendingSenderIROBTest;

    /* all integers here are in host byte order */
    //u_long next_seqno;

    resume_handler_t resume_handler;
    void *rh_arg;

    bool announced;
    bool end_announced;
    bool acked;

    // for sending partial chunks
    u_long next_seqno_to_send;
    //size_t next_chunk;
    //size_t chunk_offset;

    std::deque<struct irob_chunk_data> sent_chunks;
    std::deque<struct irob_chunk_data> resend_chunks;

    size_t num_bytes; // number of bytes added by the application
    size_t irob_offset;  // number of bytes given to senders

    std::vector<struct iovec> get_bytes_internal(size_t offset, ssize_t& len);
    std::deque<struct irob_chunk_data>::iterator find_app_chunk(size_t offset);
    
    // only one thread at a time should be sending app data
    //bool chunk_in_flight;

    // true iff the receiver might be missing some data due to
    //  a network failure.
    bool data_check;
};

#endif
