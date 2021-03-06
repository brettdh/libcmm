#ifndef irob_scheduling_h_incl
#define irob_scheduling_h_incl

#include <sys/types.h>
#include "libcmm.h"
#include "libcmm_irob.h"
#include "cmm_socket_control.h"
#include <set>
#include <algorithm>
#include <functional>

struct IROBSchedulingIndexes;

struct IROBSchedulingData {
    IROBSchedulingData();
    IROBSchedulingData(irob_id_t id, bool chunks_ready_,
                       u_long send_labels_=0);
    IROBSchedulingData(irob_id_t id, 
                       resend_request_type_t resend_request_,
                       u_long send_labels_=0);
    IROBSchedulingData(irob_id_t id, struct timeval completion_time_,
                       u_long send_labels_=0);

    bool operator<(const IROBSchedulingData& other) const;

    irob_id_t id;
    bool chunks_ready;
    resend_request_type_t resend_request;
    struct timeval completion_time; // for ACKs
    bool data_check;

    u_long send_labels;
    // more scheduling hints here?

  private:
    friend class IROBPrioritySet;
};

class IROBPrioritySet {
    typedef std::set<IROBSchedulingData> TaskSet;
  public:
    IROBPrioritySet(const std::string& level_, const std::string& type_);
    void insert(IROBSchedulingData data);
    bool pop(IROBSchedulingData& data);
    bool remove(irob_id_t id, IROBSchedulingData& data);
    void clear() { tasks.clear(); }
    
    bool empty() const { return tasks.empty(); }
    size_t size() const { return tasks.size(); }

    typedef TaskSet::iterator iterator;
    iterator begin() const { return tasks.begin(); }
    iterator end() const { return tasks.end(); }
    
    template <typename InputIterator>
    void insert_range(InputIterator head, InputIterator tail) {
        std::for_each(head, tail, 
                      std::bind1st(std::mem_fun(&IROBPrioritySet::insert), 
                                   this));
    }
    void erase(iterator head, iterator tail) {
        (void)tasks.erase(head, tail);
    }
  private:
    friend class IROBSchedulingData;
    friend class IROBSchedulingIndexes;
    TaskSet tasks;

    // "csocket", "multisocket", or "received irobs", depending on where it lives
    // (just for debugging info)
    std::string level;
    std::string type;

    void transfer(irob_id_t id, u_long new_labels,
                  IROBPrioritySet& other);
};

struct IROBSchedulingIndexes {
    enum type {
        NEW_IROBS = 0,
        NEW_CHUNKS,
        FINISHED_IROBS,
        WAITING_ACKS,
        RESEND_REQUESTS,
        WAITING_DATA_CHECKS,
        
        NUM_SETS
    };
    
    explicit IROBSchedulingIndexes(const std::string& level);

    // Copy all of other's data items to this.
    void add(const IROBSchedulingIndexes& other);
    void clear();

    size_t size() const;

    IROBPrioritySet new_irobs;
    IROBPrioritySet new_chunks;
    IROBPrioritySet finished_irobs;

    IROBPrioritySet waiting_acks;

    IROBPrioritySet resend_requests;
    IROBPrioritySet waiting_data_checks;

    void insert(IROBSchedulingData data, type index_type);
  private:
    void transfer(irob_id_t id, u_long new_labels,
                  IROBSchedulingIndexes& other);
    
    IROBPrioritySet *sets[NUM_SETS];
};

#endif
