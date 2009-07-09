#include "pending_receiver_irob.h"

PendingReceiverIROB::PendingReceiverIROB(struct begin_irob_data begin_irob)
    : PendingIROB(begin_irob), offset(0), num_bytes(0)
{
    partial_chunk.data = NULL;
    partial_chunk.datalen = 0;
}

void
PendingReceiverIROB::add_chunk(struct irob_chunk& chunk)
{
    bool result = PendingIROB::add_chunk(chunk);
    if (result) {
        num_bytes += chunk.datalen;
    }
    return result;
}

bool 
PendingReceiverIROB::is_released(void)
{
    return deps.empty();
}

ssize_t 
PendingReceiverIROB::read_data(void *buf, size_t len)
{
    ssize_t bytes_copied = 0;

    if (partial_chunk.data) {
        ssize_t bytes = min(len, partial_chunk.datalen - offset);
        assert(bytes > 0);
        memcpy(buf, partial_chunk.data + offset, bytes);
        if (len >= (partial_chunk.datalen - offset)) {
            delete [] partial_chunk.data;
            partial_chunk.data = NULL;
            partial_chunk.datalen = 0;
            offset = 0;
        } else {
            offset += bytes;
        }
        len -= bytes;
        bytes_copied += bytes;
    }

    /* if len == 0 here, we'll skip the loop */
    while (len > 0 && !chunks.empty()) {
        struct irob_chunk chunk;
        chunks.pop(chunk);

        ssize_t bytes = min(len, chunk.datalen);
        memcpy(buf + bytes_copied, chunk.data, bytes);
        bytes_copied += bytes;
        if (chunk.datalen > len) {
            offset = chunk.datalen - len;
            partial_chunk = chunk;
        } else {
            delete [] chunk.data;
        }
        len -= bytes;
    }
    num_bytes -= bytes_copied;
    return bytes_copied;
}

ssize_t 
PendingReceiverIROB::numbytes()
{
    return num_bytes;
}

PendingReceiverIROBLattice::
PendingReceiverIROBLattice(PendingIROBLattice::Predicate p)
    : pred(p)
{
}


/* There's a race on partially_read_irob between get_ready_irob and 
 * partially_read below, but they are only called sequentially. */
PendingReceiverIROB *
PendingReceiverIROBLattice::get_ready_irob()
{
    PendingReceiverIROB *pirob = NULL;
    if (partially_read_irob) {
        pirob = partially_read_irob;
        partially_read_irob = NULL;
    } else {
        /* TODO: nonblocking */
        ready_irobs.pop(pirob);
    }
    return pirob;
}

void
PendingReceiverIROBLattice::partially_read(PendingReceiverIROB *pirob)
{
    assert(partially_read_irob == NULL);
    partially_read_irob = pirob;
}

void
PendingReceiverIROBLattice::mark_ready(PendingReceiverIROB *pirob)
{
    if (pred(pirob)) {
        ready_irobs.push(pirob);
    }
}

void 
PendingReceiverIROBLattice::release_dependents(PendingReceiverIROB *pirob)
{
    for (irob_id_set::iterator it = dependents.begin();
         it != dependents.end(); it++) {
        PendingIROBHash::accessor ac;
        if (!pending_irobs.find(ac, *it)) {
            continue;
        }
        PendingIROB *pirob = ac->second;
        assert(pirob);
        pirob->dep_satisfied(pirob->id);
    }
}
