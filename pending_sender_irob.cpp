#include "pending_irob.h"
#include "pending_sender_irob.h"
#include "debug.h"
#include <vector>
#include <functional>
#include <deque>
#include <algorithm>
using std::deque;
using std::vector; using std::min;
using std::for_each;

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
      num_bytes(datalen), irob_offset(0),
      //chunk_in_flight(false),
      data_check(false)
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
        dbgprintf("Gathering %zd bytes from chunk %lu chunk_offset %zd\n",
                  bytes, it->seqno, cur_chunk_offset);
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
    dbgprintf("   (%zd bytes requested; %d chunks total; irob_offset %d\n",
              bytes_requested, (int)chunks.size(), irob_offset);
    dbgprintf("   %zd unsent bytes ready, %zu chunks waiting to resend)\n",
              num_bytes - irob_offset, resend_chunks.size());

    if (bytes_requested <= 0) {
        bytes_requested = num_bytes - irob_offset;
    }
    
    if (!resend_chunks.empty()) {
        // 1) Grab a chunk
        struct irob_chunk_data chunk = resend_chunks.front();
        resend_chunks.pop_front();
        
        // 2) Find its data + copy it to an iovec
        //   (Don't return data that pertains to more than one seqno.)
        //   (i.e. one resend request <= one seqno of data)
        //ssize_t len = min(bytes_requested, (ssize_t)chunk.datalen);
        ssize_t len = (ssize_t)chunk.datalen;
        ssize_t lencopy = len;
        data = get_bytes_internal(chunk.offset, len);
        
        // if this data was already sent once, it must be in the IROB
        assert(len == lencopy);
        
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

//     ssize_t bytes_gathered = 0;
//     size_t chunk_index = next_chunk;
//     size_t cur_chunk_offset = chunk_offset;
//     while (bytes_gathered < bytes_requested && chunk_index < chunks.size()) {
//         struct iovec next_buf;

//         ssize_t bytes = chunks[chunk_index].datalen - cur_chunk_offset;
//         if ((bytes + bytes_gathered) > bytes_requested) {
//             bytes = bytes_requested - bytes_gathered;
//         }
//         next_buf.iov_len = bytes;
//         next_buf.iov_base = chunks[chunk_index].data + cur_chunk_offset;
//         data.push_back(next_buf);
//         bytes_gathered += bytes;

//         cur_chunk_offset = 0;
//         chunk_index++;
//     }

    data = get_bytes_internal(irob_offset, bytes_requested);
    if (bytes_requested == 0) {
        dbgprintf("...no bytes ready\n");
        return data;
    }

    dbgprintf("...returning %d bytes, seqno %lu\n",
              bytes_requested, next_seqno_to_send);

    seqno = next_seqno_to_send++;
    offset_ = irob_offset;
    irob_offset += bytes_requested;

    struct irob_chunk_data sent_chunk;
    memset(&sent_chunk, 0, sizeof(sent_chunk));
    sent_chunk.seqno = seqno;
    sent_chunk.offset = offset_;
    sent_chunk.datalen = bytes_requested;
    assert(sent_chunks.size() == seqno);
    sent_chunks.push_back(sent_chunk);
    
    return data;
}


// return true iff the data specified by the chunk has been marked sent.
// bool
// PendingSenderIROB::sent(struct irob_chunk_data chunk)
// {
    
// }

/*
void 
PendingSenderIROB::mark_sent(struct irob_chunk_data sent_chunk)
{
    dbgprintf("Marking bytes %zu-%zu sent for IROB %ld\n", 
              seht_chunk.offset, sent_chunk.offset + sent_chunk.datalen, id);
    dbgprintf("   (%d chunks total; next_chunk %d chunk_offset %d irob_offset %d\n",
              (int)chunks.size(), next_chunk, chunk_offset, irob_offset);

    assert (next_chunk < chunks.size());
    while (bytes_sent > 0) {
        ssize_t chunk_bytes_left = chunks[next_chunk].datalen - chunk_offset;
        ssize_t bytes = min(chunk_bytes_left, bytes_sent);
        bytes_sent -= bytes;
        chunk_offset += bytes;
        irob_offset += bytes;
        
        if (chunk_offset == chunks[next_chunk].datalen) {
            next_chunk++;
            chunk_offset = 0;
        }
    }
    next_seqno_to_send++;
}
*/

void
PendingSenderIROB::mark_not_received(u_long seqno)//, size_t offset, size_t len)
{
    if (seqno > sent_chunks.size()) {
        dbgprintf("ERROR: resend requested for seqno %lu, but IROB %ld "
                  "only has %zu sent chunks\n", seqno, id, sent_chunks.size());
    } else {
        resend_chunks.push_back(sent_chunks[seqno]);
    }
}

void
PendingSenderIROB::request_data_check()
{
    data_check = true;
}

bool
PendingSenderIROB::needs_data_check()
{
    //return data_check;
    // see csocket_sender.cpp; DATA_CHECK is obsolete.
    return false;
}

/*
void
PendingSenderIROB::rewind(size_t pos)
{
    dbgprintf("Resetting send pointer for IROB %ld\n", id);
    next_seqno_to_send = 0;
    next_chunk = 0;
    chunk_offset = 0;
    offset = 0;

    // a call to rewind() means that the sender and receiver
    //  agree about the amount of remaining data
    data_check = false;

    mark_sent(pos);
}
*/
