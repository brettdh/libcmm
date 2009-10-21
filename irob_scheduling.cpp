#include "irob_scheduling.h"
#include <set>
using std::set;
#include "common.h"
#include "debug.h"

IROBSchedulingData::IROBSchedulingData()
{
    /* empty */
}

IROBSchedulingData::IROBSchedulingData(irob_id_t id_, bool chunks_ready_,
                                       u_long send_labels_)
    : id(id_), chunks_ready(chunks_ready_),
      resend_request(CMM_RESEND_REQUEST_NONE),
      send_labels(send_labels_)
{
}

IROBSchedulingData::IROBSchedulingData(irob_id_t id_,
                                       resend_request_type_t resend_request_,
                                       u_long send_labels_)
    : id(id_), chunks_ready(false),
      resend_request(resend_request_), send_labels(send_labels_)
{
}

bool 
IROBSchedulingData::operator<(const IROBSchedulingData& other) const
{
    // can implement priority here, based on 
    //  any added scheduling hints
    if ((send_labels & CMM_LABEL_ONDEMAND) && 
        !(other.send_labels & CMM_LABEL_ONDEMAND)) {
        return true;
    } else if (!(send_labels & CMM_LABEL_ONDEMAND) && 
               (other.send_labels & CMM_LABEL_ONDEMAND)) {
        return false;
    }

    return ((owner && (owner->send_labels & send_labels)) ||
            (id < other.id));
}

void 
IROBPrioritySet::insert(IROBSchedulingData data)
{
    //TODO: do something more interesting.
    dbgprintf("Inserting scheduling request for IROB %ld (%s)\n",
              data.id, data.chunks_ready ? "chunk" : "irob");
    data.owner = owner;
    tasks.insert(data);
}

bool 
IROBPrioritySet::pop(IROBSchedulingData& data)
{
    bool ret = pop_item(tasks, data);
    if (ret) {
        dbgprintf("Grabbing scheduling request for IROB %ld (%s)\n",
                  data.id, data.chunks_ready ? "chunk" : "irob");
    }
    return ret;
}

IROBSchedulingIndexes::IROBSchedulingIndexes(u_long send_labels_) 
    : send_labels(send_labels_) 
{
    new_irobs.owner = this;
    new_chunks.owner = this;
    finished_irobs.owner = this;
    waiting_acks.owner = this;
    resend_requests.owner = this;
}

void
IROBSchedulingIndexes::add(const IROBSchedulingIndexes& other)
{
    new_irobs.insert_range(other.new_irobs.begin(), other.new_irobs.end());
    new_chunks.insert_range(other.new_chunks.begin(), other.new_chunks.end());
    finished_irobs.insert_range(other.finished_irobs.begin(),
                                other.finished_irobs.end());
    waiting_acks.insert_range(other.waiting_acks.begin(), other.waiting_acks.end());
    resend_requests.insert_range(other.resend_requests.begin(), 
                                 other.resend_requests.end());
}
