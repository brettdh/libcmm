#ifndef irob_data_chunk_deque_h_incl
#define irob_data_chunk_deque_h_incl

#include <deque>
#include "cmm_socket_control.h"

class IROBDataChunkDeque {
    typedef std::deque<struct irob_chunk_data> DequeType;
    DequeType chunks;
    int incomplete_chunks;

  public:
    IROBDataChunkDeque();

    bool all_complete() const;

    bool empty() const;
    size_t size() const;
    void resize(size_t new_size, struct irob_chunk_data empty_data);
    void clear();

    void setChunkData(struct irob_chunk_data replacement_chunk);

    const struct irob_chunk_data& operator[](size_t n) const;
    const struct irob_chunk_data& front() const;
    const struct irob_chunk_data& back() const;

    void push_front(const struct irob_chunk_data& chunk);
    void push_back(const struct irob_chunk_data& chunk);

    void pop_front();
    void pop_back();

    DequeType::const_iterator begin() const;
    DequeType::const_iterator end() const;
};

#endif
