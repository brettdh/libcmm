#include "irob_data_chunk_deque.h"

IROBDataChunkDeque::IROBDataChunkDeque()
    : incomplete_chunks(0)
{
}

bool
IROBDataChunkDeque::all_complete() const
{
    return (incomplete_chunks == 0);
}

bool
IROBDataChunkDeque::empty() const
{
    return chunks.empty();
}

size_t 
IROBDataChunkDeque::size() const
{
    return chunks.size();
}

void 
IROBDataChunkDeque::resize(size_t new_size, struct irob_chunk_data empty_data)
{
    size_t cur_size = chunks.size();
    if (new_size > cur_size) {
        incomplete_chunks += (new_size - cur_size);
    }
    chunks.resize(new_size, empty_data);
}

void
IROBDataChunkDeque::clear()
{
    chunks.clear();
    incomplete_chunks = 0;
}

void
IROBDataChunkDeque::setChunkData(unsigned long seqno, char *data, size_t datalen)
{
    assert(seqno < chunks.size());
    struct irob_chunk_data& chunk = chunks[seqno];
    assert(chunk.data == NULL);
    incomplete_chunks--;
    chunk.datalen = datalen;
    chunk.data = data;
}

const struct irob_chunk_data& 
IROBDataChunkDeque::operator[](size_t n) const
{
    return chunks[n];
}

const struct irob_chunk_data& 
IROBDataChunkDeque::front() const
{
    return chunks.front();
}

const struct irob_chunk_data& 
IROBDataChunkDeque::back() const
{
    return chunks.back();
}


void 
IROBDataChunkDeque::push_front(const struct irob_chunk_data& chunk)
{
    chunks.push_front(chunk);
    if (chunk.data == NULL) {
        incomplete_chunks++;
    }
}

void 
IROBDataChunkDeque::push_back(const struct irob_chunk_data& chunk)
{
    chunks.push_back(chunk);
    if (chunk.data == NULL) {
        incomplete_chunks++;
    }
}


void 
IROBDataChunkDeque::pop_front()
{
    if (chunks.front().data == NULL) {
        incomplete_chunks--;
    }
    chunks.pop_front();
}

void 
IROBDataChunkDeque::pop_back()
{
    if (chunks.back().data == NULL) {
        incomplete_chunks--;
    }
    chunks.pop_back();
}


IROBDataChunkDeque::DequeType::const_iterator 
IROBDataChunkDeque::begin() const
{
    return chunks.begin();
}

IROBDataChunkDeque::DequeType::const_iterator 
IROBDataChunkDeque::end() const
{
    return chunks.end();
}
