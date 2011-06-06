#ifndef irob_data_chunk_deque_h_incl
#define irob_data_chunk_deque_h_incl

#include <deque>
#include "cmm_socket_control.h"

class IROBDataChunkDeque {
    typedef std::deque<struct irob_chunk_data> DequeType;
    DequeType chunks;

  public:
    bool all_complete() const;

    bool empty() const;
    size_t size() const;
    void resize(size_t new_size, struct irob_chunk_data empty_data);
    void clear();

    struct irob_chunk_data& operator[](size_t n);
    const struct irob_chunk_data& operator[](size_t n) const;
    struct irob_chunk_data& front();
    const struct irob_chunk_data& front() const;
    struct irob_chunk_data& back();
    const struct irob_chunk_data& back() const;

    void push_front(const struct irob_chunk_data& chunk);
    void push_back(const struct irob_chunk_data& chunk);

    void pop_front();
    void pop_back();

    DequeType::iterator begin();
    DequeType::const_iterator begin() const;
    DequeType::iterator end();
    DequeType::const_iterator end() const;
};

#endif
