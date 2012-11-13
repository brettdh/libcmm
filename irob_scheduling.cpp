#include "irob_scheduling.h"
#include <set>
#include <string>
using std::set; using std::string;
#include "common.h"
#include "debug.h"
#include "timeops.h"

IROBSchedulingData::IROBSchedulingData()
    : id(-1), chunks_ready(false),
      resend_request(CMM_RESEND_REQUEST_NONE),
      data_check(false), send_labels(0)
{
    completion_time.tv_sec = -1;
    completion_time.tv_usec = 0;
}

IROBSchedulingData::IROBSchedulingData(irob_id_t id_, bool chunks_ready_,
                                       u_long send_labels_)
    : id(id_), chunks_ready(chunks_ready_),
      resend_request(CMM_RESEND_REQUEST_NONE),
      data_check(false), send_labels(send_labels_)
{
    completion_time.tv_sec = -1;
    completion_time.tv_usec = 0;
}

IROBSchedulingData::IROBSchedulingData(irob_id_t id_,
                                       resend_request_type_t resend_request_,
                                       u_long send_labels_)
    : id(id_), chunks_ready(false),
      resend_request(resend_request_), data_check(false), send_labels(send_labels_)
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
      data_check(false), send_labels(send_labels_)
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

    return ((timercmp(&completion_time, &other.completion_time, <)) ||
            (id < other.id));
}

IROBPrioritySet::IROBPrioritySet(const string& level_, const string& type_)
    : level(level_), type(type_)
{
}

void 
IROBPrioritySet::insert(IROBSchedulingData data)
{
    dbgprintf("Inserting %s-level scheduling request for IROB %ld (%s)\n",
              level.c_str(), data.id, type.c_str());
    tasks.insert(data);
}

bool 
IROBPrioritySet::pop(IROBSchedulingData& data)
{
    bool ret = pop_item(tasks, data);
    if (ret) {
        dbgprintf("Grabbing %s-level scheduling request for IROB %ld (%s)\n",
                  level.c_str(), data.id, type.c_str());
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
        dbgprintf("Grabbing %s-level scheduling request for IROB %ld (%s)\n",
                  level.c_str(), data.id, type.c_str());
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

IROBSchedulingIndexes::IROBSchedulingIndexes(const string& level) 
    : new_irobs(level, "new irobs"),
      new_chunks(level, "new chunks"),
      finished_irobs(level, "finished irobs"),
      waiting_acks(level, "waiting acks"),
      resend_requests(level, "resend requests"),
      waiting_data_checks(level, "waiting data-checks")
{
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
