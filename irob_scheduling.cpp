#include "irob_scheduling.h"
#include <set>
using std::set;
#include "common.h"
#include "debug.h"
#include "timeops.h"

IROBSchedulingData::IROBSchedulingData()
    : id(-1), chunks_ready(false),
      resend_request(CMM_RESEND_REQUEST_NONE),
      data_check(false), send_labels(0), owner(NULL)
{
    completion_time.tv_sec = -1;
    completion_time.tv_usec = 0;
}

IROBSchedulingData::IROBSchedulingData(irob_id_t id_, bool chunks_ready_,
                                       u_long send_labels_)
    : id(id_), chunks_ready(chunks_ready_),
      resend_request(CMM_RESEND_REQUEST_NONE),
      data_check(false), send_labels(send_labels_), owner(NULL)
{
    completion_time.tv_sec = -1;
    completion_time.tv_usec = 0;
}

IROBSchedulingData::IROBSchedulingData(irob_id_t id_,
                                       resend_request_type_t resend_request_,
                                       u_long send_labels_)
    : id(id_), chunks_ready(false),
      resend_request(resend_request_), data_check(false), send_labels(send_labels_), owner(NULL)
{
    completion_time.tv_sec = -1;
    completion_time.tv_usec = 0;
}

IROBSchedulingData::IROBSchedulingData(irob_id_t id_, 
                                       struct timeval completion_time_,
                                       u_long send_labels_)
    : id(id_), chunks_ready(false), 
      resend_request(CMM_RESEND_REQUEST_NONE),
      completion_time(completion_time_),
      data_check(false), send_labels(send_labels_), owner(NULL)
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

    return (//(owner && (owner->send_labels & send_labels)) ||
            (timercmp(&completion_time, &other.completion_time, <)) ||
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

bool
IROBPrioritySet::remove(irob_id_t id, IROBSchedulingData& data)
{
    IROBSchedulingData dummy(id, false);
    TaskSet::iterator pos = tasks.find(dummy);
    if (pos != tasks.end()) {
        data = *pos;
        tasks.erase(pos);
        dbgprintf("Grabbing scheduling request for IROB %ld (%s)\n",
                  data.id, data.chunks_ready ? "chunk" : "irob");
        return true;
    }
    return false;
}

void
IROBPrioritySet::transfer(irob_id_t id, u_long new_labels,
                          IROBPrioritySet& other)
{
    IROBSchedulingData data;
    if (remove(id, data)) {
        data.send_labels = new_labels;
        other.insert(data);
    }
}

IROBSchedulingIndexes::IROBSchedulingIndexes(u_long send_labels_) 
    : send_labels(send_labels_) 
{
    new_irobs.owner = this;
    new_chunks.owner = this;
    finished_irobs.owner = this;
    waiting_acks.owner = this;
    resend_requests.owner = this;
    waiting_data_checks.owner = this;
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
    waiting_data_checks.insert_range(other.waiting_data_checks.begin(), 
                                     other.waiting_data_checks.end());
}

void
IROBSchedulingIndexes::clear()
{
    new_irobs.clear();
    new_chunks.clear();
    finished_irobs.clear();
    waiting_acks.clear();
    resend_requests.clear();
    waiting_data_checks.clear();
}

size_t
IROBSchedulingIndexes::size() const
{
    return (new_irobs.size() +
            new_chunks.size() +
            finished_irobs.size() +
            waiting_acks.size() +
            resend_requests.size() +
            waiting_data_checks.size());
}

/* transfer the IROB from this to other, giving it the new labels.
 * this and other may be the same object. */
void
IROBSchedulingIndexes::transfer(irob_id_t id, u_long new_labels, 
                                IROBSchedulingIndexes& other)
{
    new_irobs.transfer(id, new_labels, other.new_irobs);
    new_chunks.transfer(id, new_labels, other.new_chunks);
}
