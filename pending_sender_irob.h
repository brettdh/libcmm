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

#include "csocket.h"
#include <arpa/inet.h>

#include <set>
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
    std::vector<struct iovec> get_ready_bytes(CSocket * csock, 
                                              ssize_t& bytes_requested, 
                                              u_long& seqno,
                                              size_t& offset);

    // returns the number of bytes ready to be sent on csock,
    //  without advancing the pointer.  Ignores bytes already
    //  sent on csock.
    size_t num_ready_bytes(CSocket *csock);

    std::vector<struct iovec> 
        get_last_sent_chunk_htonl(CSocket * csock,
                                  struct irob_chunk_data *chunk);

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
    bool all_bytes_chunked(); // true if all bytes have been put into send-chunks.
    size_t num_sender_chunks();

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

    // must be holding sk->scheduling_state_lock
    void markSentOn(CSocketPtr csock);
    // must be holding sk->scheduling_state_lock
    bool wasSentOn(in_addr_t local_ip, in_addr_t remote_ip);

    bool was_announced(CSocket * csock);
    bool end_was_announced(CSocket * csock);
    void mark_announcement_sent(CSocket * csock);
    void mark_end_announcement_sent(CSocket * csock);

    void get_thunk(resume_handler_t& rh, void *& arg);

    bool should_send_on_all_networks();
    void mark_send_on_all_networks();

  private:
    friend class PendingSenderIROBTest;

    resume_handler_t resume_handler;
    void *rh_arg;

    //bool announced;
    //bool end_announced;
    std::map<CSocket *, bool> announcements;
    std::map<CSocket *, bool> end_announcements;
    bool acked;

    // for sending partial chunks
    //u_long next_seqno_to_send;
    std::map<CSocket *, u_long> next_seqnos_to_send;
    u_long get_next_seqno_to_send(CSocket *csock);
    void increment_seqno(CSocket *csock);

    std::deque<struct irob_chunk_data> sent_chunks;

    // make this a set so we don't have duplicate chunk-resend requests.
    typedef std::set<struct irob_chunk_data> ResendChunkSet;
    ResendChunkSet resend_chunks;

    size_t num_bytes; // number of bytes added by the application

    // number of bytes given to senders
    //size_t irob_offset;
    std::map<CSocket *, size_t> irob_offsets;
    size_t get_irob_offset(CSocket *csock);
    void increment_irob_offset(CSocket *csock, size_t increment);

    void add_sent_chunk(CSocket *csock, ssize_t len);

    // default: false.
    bool send_on_all_networks;

    CSocket *null_index_if_single_sending(CSocket *csock);

    std::vector<struct iovec> get_bytes_internal(size_t offset, ssize_t& len);
    std::deque<struct irob_chunk_data>::iterator find_app_chunk(size_t offset);
    
    typedef std::set<std::pair<in_addr_t, in_addr_t> > IfacePairSet;
    IfacePairSet sending_ifaces;
};

#endif
