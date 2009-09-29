#ifndef irob_scheduling_h_incl
#define irob_scheduling_h_incl

#include <sys/types.h>
#include "libcmm.h"
#include "libcmm_irob.h"
#include "cmm_socket_control.h"
#include <set>

struct IROBSchedulingIndexes;

struct IROBSchedulingData {
    IROBSchedulingData(irob_id_t id=-1, u_long seqno=INVALID_IROB_SEQNO,
                       u_long send_labels_=0);
    bool operator<(const IROBSchedulingData& other) const;

    irob_id_t id;
    u_long seqno; // may be INVALID_IROB_SEQNO
    u_long send_labels;
    // more scheduling hints here?

    struct IROBSchedulingIndexes *owner;
};

class IROBPrioritySet {
    typedef std::set<IROBSchedulingData> TaskSet;
  public:
    void insert(IROBSchedulingData data);
    bool pop(IROBSchedulingData& data);
    bool empty() const { return tasks.empty(); }

    typedef TaskSet::iterator iterator;
    iterator begin() { return tasks.begin(); }
    iterator end() { return tasks.end(); }
    
    template <typename InputIterator>
    void insert(InputIterator head, InputIterator tail) {
        (void)tasks.insert(head, tail);
    }
    void erase(iterator head, iterator tail) {
        (void)tasks.erase(head, tail);
    }
  private:
    TaskSet tasks;
};

struct IROBSchedulingIndexes {
    IROBSchedulingIndexes(u_long send_labels_) : send_labels(send_labels_) {}

    IROBPrioritySet new_irobs;
    IROBPrioritySet new_chunks;
    IROBPrioritySet finished_irobs;

    IROBPrioritySet waiting_acks;

    u_long send_labels;
};

#endif
