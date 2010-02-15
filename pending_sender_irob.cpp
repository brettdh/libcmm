#include "pending_irob.h"
#include "pending_sender_irob.h"
#include "debug.h"
#include <vector>
#include <functional>
using std::vector; using std::min;

PendingSenderIROB::PendingSenderIROB(irob_id_t id_, 
                                     int numdeps, const irob_id_t *deps_array,
                                     size_t datalen, char *data,
				     u_long send_labels,
                                     resume_handler_t resume_handler_, 
                                     void *rh_arg_)
    : PendingIROB(id_, numdeps, deps_array, datalen, data, send_labels),
      next_seqno(INVALID_IROB_SEQNO + 1),
      resume_handler(resume_handler_), rh_arg(rh_arg_),
      announced(false), end_announced(false), acked(false),
      next_seqno_to_send(next_seqno), next_chunk(0), chunk_offset(0),
      offset(0),
      chunk_in_flight(false),
      data_check(false)
{
}

bool
PendingSenderIROB::add_chunk(struct irob_chunk_data& irob_chunk)
{
    if (is_complete()) {
        return false;
    }

    irob_chunk.seqno = next_seqno++;
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

vector<struct iovec> 
PendingSenderIROB::get_ready_bytes(ssize_t& bytes_requested, u_long& seqno,
                                   size_t &offset_) const
{
    vector<struct iovec> data;

    dbgprintf("Getting bytes to send from IROB %ld\n", id);
    dbgprintf("   (%d chunks total; next_chunk %d chunk_offset %d offset %d\n",
              (int)chunks.size(), next_chunk, chunk_offset, offset);

    if (chunks.empty() || next_chunk >= chunks.size()) {
        bytes_requested = 0;
        dbgprintf("...no bytes ready\n");
        return data;
    }

    if (bytes_requested <= 0) {
        bytes_requested = chunks[next_chunk].datalen - chunk_offset;
    }
    
    ssize_t bytes_gathered = 0;
    size_t chunk_index = next_chunk;
    size_t cur_chunk_offset = chunk_offset;
    while (bytes_gathered < bytes_requested && chunk_index < chunks.size()) {
        struct iovec next_buf;
        ssize_t bytes = chunks[chunk_index].datalen - cur_chunk_offset;
        if ((bytes + bytes_gathered) > bytes_requested) {
            bytes = bytes_requested - bytes_gathered;
        }
        next_buf.iov_len = bytes;
        next_buf.iov_base = chunks[chunk_index].data + cur_chunk_offset;
        data.push_back(next_buf);
        bytes_gathered += bytes;

        cur_chunk_offset = 0;
        chunk_index++;
    }

    dbgprintf("...returning %d bytes, seqno %lu\n",
              bytes_gathered, next_seqno_to_send);

    bytes_requested = bytes_gathered;
    seqno = next_seqno_to_send;
    offset_ = offset;
    return data;
}

void 
PendingSenderIROB::mark_sent(ssize_t bytes_sent)
{
    dbgprintf("Advancing send pointer by %d for IROB %ld\n", bytes_sent, id);
    dbgprintf("   (%d chunks total; next_chunk %d chunk_offset %d offset %d\n",
              (int)chunks.size(), next_chunk, chunk_offset, offset);

    assert (next_chunk < chunks.size());
    while (bytes_sent > 0) {
        ssize_t chunk_bytes_left = chunks[next_chunk].datalen - chunk_offset;
        ssize_t bytes = min(chunk_bytes_left, bytes_sent);
        bytes_sent -= bytes;
        chunk_offset += bytes;
        offset += bytes;
        
        if (chunk_offset == chunks[next_chunk].datalen) {
            next_chunk++;
            chunk_offset = 0;
        }
    }
    next_seqno_to_send++;
}

void
PendingSenderIROB::request_data_check()
{
    data_check = true;
}

bool
PendingSenderIROB::needs_data_check()
{
    return data_check;
}

void
PendingSenderIROB::rewind(size_t pos)
{
    dbgprintf("Resetting send pointer for IROB %ld\n", id);
    next_seqno_to_send = INVALID_IROB_SEQNO + 1;
    next_chunk = 0;
    chunk_offset = 0;
    offset = 0;

    // a call to rewind() means that the sender and receiver
    //  agree about the amount of remaining data
    data_check = false;

    mark_sent(pos);
}
