#include "irob_data_chunk_deque.h"


bool
IROBDataChunkDeque::all_complete() const
{
    for (size_t i = 0; i < chunks.size(); ++i) {
        if (chunks[i].data() == NULL) {
            return false;
        }
    }
    return true;
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
    chunks.resize(new_size, empty_data);
}

void
IROBDataChunkDeque::clear()
{
    chunks.clear();
}

struct irob_chunk_data& 
IROBDataChunkDeque::operator[](size_t n)
{
    return chunks[n];
}

const struct irob_chunk_data& 
IROBDataChunkDeque::operator[](size_t n) const
{
    return chunks[n];
}

struct irob_chunk_data& 
IROBDataChunkDeque::front()
{
    return chunks.front();
}

const struct irob_chunk_data& 
IROBDataChunkDeque::front() const
{
    return chunks.front();
}

struct irob_chunk_data& 
IROBDataChunkDeque::back()
{
    return chunks.back();
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
}

void 
IROBDataChunkDeque::push_back(const struct irob_chunk_data& chunk)
{
    chunks.push_back(chunk);
}


void 
IROBDataChunkDeque::pop_front()
{
    chunks.pop_front();
}

void 
IROBDataChunkDeque::pop_back()
{
    chunks.pop_back();
}


IROBDataChunkDeque::DequeType::iterator 
IROBDataChunkDeque::begin()
{
    return chunks.begin();
}

IROBDataChunkDeque::DequeType::const_iterator 
IROBDataChunkDeque::begin() const
{
    return chunks.begin();
}

IROBDataChunkDeque::DequeType::iterator 
IROBDataChunkDeque::end()
{
    return chunks.end();
}

IROBDataChunkDeque::DequeType::const_iterator 
IROBDataChunkDeque::end() const
{
    return chunks.end();
}
