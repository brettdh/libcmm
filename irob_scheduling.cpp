#include "irob_scheduling.h"
#include <set>
using std::set;
#include "common.h"

IROBSchedulingData::IROBSchedulingData()
{
    /* empty */
}

IROBSchedulingData::IROBSchedulingData(irob_id_t id_, bool chunks_ready_,
                                       u_long send_labels_)
    : id(id_), send_labels(send_labels_)
{
    data.chunks_ready = chunks_ready_;
}

IROBSchedulingData::IROBSchedulingData(irob_id_t id_,
                                       resend_request_type_t resend_request_,
                                       u_long send_labels_)
    : id(id_), send_labels(send_labels_)
{
    data.resend_request = resend_request_;
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
    data.owner = owner;
    tasks.insert(data);
}

bool 
IROBPrioritySet::pop(IROBSchedulingData& data)
{
    return pop_item(tasks, data);
}

IROBSchedulingIndexes::IROBSchedulingIndexes(u_long send_labels_) 
    : send_labels(send_labels_) 
{
    new_irobs.owner = this;
    new_chunks.owner = this;
    finished_irobs.owner = this;
    waiting_acks.owner = this;

}
