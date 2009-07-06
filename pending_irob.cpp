#include "pending_irob.h"

PendingIROB::PendingIROB(struct begin_irob_data begin_irob)
    : id(ntohl(begin_irob.id)), 
      send_labels(begin_irob.send_labels), 
      recv_labels(begin_irob.recv_labels),
      anonymous(begin_irob.numdeps == -1),
      complete(false)
{
    int numdeps = ntohl(begin_irob.numdeps);
    if (numdeps > 0) {
        assert(begin_irob.deps);
    } else if (numdeps == -1) {
        anonymous = true;
    }
    for (int i = 0; i < numdeps; i++) {
        deps.insert(ntohl(begin_irob.deps[i]));
    }
}

PendingIROB::~PendingIROB()
{
    while (!chunks.empty()) {
        struct irob_chunk_data chunk;
        chunks.pop(chunk);
        delete [] chunk.data;
    }
}

bool
PendingIROB::add_chunk(struct irob_chunk_data& irob_chunk)
{
    if (is_complete() || is_released()) {
        return false;
    }
    //irob_chunk.seqno = next_seqno++;
    chunks.push(irob_chunk);
    return true;
}

bool
PendingIROB::finish(void)
{
    if (is_complete()) {
        return false;
    }
    complete = true;
    return true;
}

void
PendingIROB::add_dependent(PendingIROB *dependent)
{
    
}

bool 
PendingIROB::is_complete(void)
{
    return complete;
}


void 
PendingIROBLattice::add(irob_id_t id, PendingIROB *pirob)
{
    struct node *pos = NULL;

    assert(pirob);
    if (nodes.find(pirob->id) != nodes.end()) {
        pos = nodes[pirob->id];
    } else {
        pos = new struct node;
        pos->id = id;
        pos->pirob = NULL;
        nodes[id] = pos;
        bottom.up.insert(pos);
    }
    assert(pos->irob == NULL);
    pos->pirob = pirob;

    add_deps(pos);
}

void
PendingIROBLattice::add_deps(struct node *pos)
{
    PendingIROB *pirob = pos->pirob;
    for (std::set<irob_id_t>::iterator it = pirob->deps.begin();
         it != pirob->deps.end(); it++) {
        irob_id_t id = *it;
        if (nodes.find(id) != nodes.end()) {
            
        }
    }
}

void 
PendingIROBLattice::remove(PendingIROB *pirob)
{

}

/* returns true if first depends on second. */
bool 
PendingIROBLattice::depends_on(PendingIROB *first, PendingIROB *second)
{
    
}

void 
PendingIROBLattice::for_each_dep(PendingIROB *dependent, iter_fn_t fn)
{
    
}
