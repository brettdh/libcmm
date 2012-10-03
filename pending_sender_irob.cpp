#include "pending_irob.h"
#include "pending_sender_irob.h"
#include "debug.h"
#include <vector>
#include <functional>
#include <deque>
#include <algorithm>
using std::deque;
using std::vector; using std::min;
using std::for_each; using std::make_pair;

PendingSenderIROB::PendingSenderIROB(irob_id_t id_, 
                                     int numdeps, const irob_id_t *deps_array,
                                     size_t datalen, char *data,
                                     u_long send_labels,
                                     resume_handler_t resume_handler_, 
                                     void *rh_arg_)
    : PendingIROB(id_, numdeps, deps_array, datalen, data, send_labels),
      resume_handler(resume_handler_), rh_arg(rh_arg_),
      announced(false), end_announced(false), acked(false),
      next_seqno_to_send(0), //next_chunk(0), chunk_offset(0),
      num_bytes(datalen), irob_offset(0)
      //chunk_in_flight(false),
{
}

bool
PendingSenderIROB::add_chunk(struct irob_chunk_data& irob_chunk)
{
    if (is_complete()) {
        return false;
    }

    irob_chunk.seqno = chunks.size();
    irob_chunk.offset = num_bytes;
    num_bytes += irob_chunk.datalen;
    chunks.push_back(irob_chunk);

    return true;
}

void
PendingSenderIROB::ack()
{
    acked = true;
}

bool
PendingSenderIROB::is_acked(void)
{
    return acked;
}

size_t
PendingSenderIROB::expected_bytes()
{
    return for_each(chunks.begin(),
                    chunks.end(),
                    SumChunkFunctor()).sum;
}

bool
PendingSenderIROB::all_chunks_sent()
{
    size_t sent_bytes = for_each(sent_chunks.begin(),
                                 sent_chunks.end(),
                                 SumChunkFunctor()).sum;
    return (sent_bytes == expected_bytes());
}

size_t
PendingSenderIROB::num_chunks_sent()
{
    return sent_chunks.size();
}

struct LessByOffset {
    bool operator()(const struct irob_chunk_data& one, 
                    const struct irob_chunk_data& other) {
        return (one.offset < other.offset);
    }
};

deque<struct irob_chunk_data>::iterator
PendingSenderIROB::find_app_chunk(size_t offset)
{
    struct irob_chunk_data dummy;
    dummy.offset = offset;
    // find the one with the greatest offset where the offset is not greater
    //  than this one
    deque<struct irob_chunk_data>::iterator it = upper_bound(chunks.begin(), 
                                                             chunks.end(), 
                                                             dummy, 
                                                             LessByOffset());
    if (!chunks.empty()) {
        const struct irob_chunk_data& target = *(it - 1);
        if (target.offset <= offset &&
            offset < (target.offset + target.datalen)) {
            --it;
        }
    }
    return it;
}

vector<struct iovec> 
PendingSenderIROB::get_bytes_internal(size_t offset, ssize_t& len)
{
    vector<struct iovec> data;
    deque<struct irob_chunk_data>::iterator it = find_app_chunk(offset);
    if (it == chunks.end()) {
        len = 0;
        return data;
    }

    ssize_t bytes_gathered = 0;
    ssize_t cur_chunk_offset = offset - it->offset;
    while (bytes_gathered < len && it != chunks.end()) {
        struct iovec next_buf;

        struct irob_chunk_data& chunk = *it;
        ssize_t bytes = chunk.datalen - cur_chunk_offset;
        if ((bytes + bytes_gathered) > len) {
            bytes = len - bytes_gathered;
        }
        next_buf.iov_len = bytes;
        next_buf.iov_base = chunk.data + cur_chunk_offset;
        dbgprintf("Gathering %d bytes from chunk %lu chunk_offset %d\n",
                  (int)bytes, it->seqno, (int)cur_chunk_offset);
        data.push_back(next_buf);
        bytes_gathered += bytes;

        cur_chunk_offset = 0;
        it++;
    }
    len = bytes_gathered;

    return data;
}

vector<struct iovec> 
PendingSenderIROB::get_ready_bytes(ssize_t& bytes_requested, u_long& seqno,
                                   size_t &offset_)
{
    vector<struct iovec> data;

    dbgprintf("Getting bytes to send from IROB %ld\n", id);
    dbgprintf("   (%d bytes requested; %d chunks total; irob_offset %d\n",
              (int)bytes_requested, (int)chunks.size(), irob_offset);
    dbgprintf("   %zd unsent bytes ready, %zu chunks waiting to resend)\n",
              num_bytes - irob_offset, resend_chunks.size());

    if (bytes_requested <= 0) {
        bytes_requested = num_bytes - irob_offset;
    }
    
    if (!resend_chunks.empty()) {
        // 1) Grab a chunk
        ResendChunkSet::iterator front = resend_chunks.begin();
        struct irob_chunk_data chunk = *front;
        resend_chunks.erase(front);
        
        // 2) Find its data + copy it to an iovec
        //   (Don't return data that pertains to more than one seqno.)
        //   (i.e. one resend request <= one seqno of data)
        //ssize_t len = min(bytes_requested, (ssize_t)chunk.datalen);
        ssize_t len = (ssize_t)chunk.datalen;
        ssize_t lencopy = len;
        data = get_bytes_internal(chunk.offset, len);
        
        // if this data was already sent once, it must be in the IROB
        ASSERT(len == lencopy);
        
        // 3) write out the argument-return values
        offset_ = chunk.offset;
        seqno = chunk.seqno;
        /* Maybe special-case this later, but for now don't,
         * since it breaks my assumption that sent_chunks are
         * always sent in a single shot.
        if ((ssize_t)chunk.datalen > bytes_requested) {
            chunk.datalen -= bytes_requested;
            chunk.offset += bytes_requested;
            resend_chunks.push_front(chunk);
        } else {
        */
        bytes_requested = len;
        //}

        // For simplicity, only return this resend request's data.
        //  The sender thread can loop around and ask for more data
        //  after it finishes with this chunk.
        return data;
    }

    data = get_bytes_internal(irob_offset, bytes_requested);
    if (bytes_requested == 0) {
        dbgprintf("...no bytes ready\n");
        return data;
    }

    dbgprintf("...returning %d bytes, seqno %lu\n",
              (int)bytes_requested, next_seqno_to_send);

    seqno = next_seqno_to_send++;
    offset_ = irob_offset;
    irob_offset += bytes_requested;

    struct irob_chunk_data sent_chunk;
    memset(&sent_chunk, 0, sizeof(sent_chunk));
    sent_chunk.id = id;
    sent_chunk.seqno = seqno;
    sent_chunk.offset = offset_;
    sent_chunk.datalen = bytes_requested;
    ASSERT(sent_chunks.size() == seqno);
    sent_chunks.push_back(sent_chunk);
    
    return data;
}

vector<struct iovec>
PendingSenderIROB::get_last_sent_chunk_htonl(struct irob_chunk_data *chunk)
{
    ASSERT(chunk);
    if (sent_chunks.empty()) {
        return vector<struct iovec>();
    }

    struct irob_chunk_data last_chunk = sent_chunks.back();
    chunk->id = htonl(last_chunk.id);
    chunk->seqno = htonl(last_chunk.seqno);
    chunk->offset = htonl(last_chunk.offset);
    chunk->datalen = htonl(last_chunk.datalen);
    chunk->data = NULL;
    
    ssize_t len = last_chunk.datalen;
    return get_bytes_internal(last_chunk.offset, len);
}

void
PendingSenderIROB::mark_not_received(u_long seqno)
{
    if (seqno > sent_chunks.size()) {
        dbgprintf("ERROR: resend requested for seqno %lu, but IROB %ld "
                  "only has %zu sent chunks\n", seqno, id, sent_chunks.size());
    } else {
        resend_chunks.insert(sent_chunks[seqno]);
    }
}

void
PendingSenderIROB::mark_drop_point(int next_chunk)
{
    for (int i = next_chunk; i < (int)sent_chunks.size(); ++i) {
        mark_not_received((u_long)i);
    }
}

// must be holding sk->scheduling_state_lock
void
PendingSenderIROB::markSentOn(CSocketPtr csock)
{
    sending_ifaces.insert(make_pair(csock->local_iface.ip_addr.s_addr,
                                    csock->remote_iface.ip_addr.s_addr));
}

static bool
matches(in_addr_t expected, in_addr_t actual)
{
    // expected == 0 means match-any
    return (expected == 0 || expected == actual);
}

// must be holding sk->scheduling_state_lock
bool
PendingSenderIROB::wasSentOn(in_addr_t local_ip, in_addr_t remote_ip)
{
    if (local_ip == 0 && remote_ip == 0) {
        return true;
    }
    for (IfacePairSet::const_iterator it = sending_ifaces.begin();
         it != sending_ifaces.end(); ++it) {
        if (matches(local_ip, it->first) &&
            matches(remote_ip, it->second)) {
            return true;
        }
    }
    return false;
}

// must be holding sk->scheduling_state_lock
bool 
PendingSenderIROB::was_announced()
{
    return announced;
}

// must be holding sk->scheduling_state_lock
bool 
PendingSenderIROB::end_was_announced()
{
    return end_announced;
}

// must be holding sk->scheduling_state_lock
void
PendingSenderIROB::mark_announcement_sent()
{
    announced = true;
}

// must be holding sk->scheduling_state_lock
void
PendingSenderIROB::mark_end_announcement_sent()
{
    end_announced = true;
}

void
PendingSenderIROB::get_thunk(resume_handler_t& rh, void *& arg)
{
    rh = resume_handler;
    arg = rh_arg;
}
