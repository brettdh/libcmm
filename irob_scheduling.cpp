#include "irob_scheduling.h"
#include <set>
using std::set;
#include "common.h"

IROBSchedulingData::IROBSchedulingData(irob_id_t id_, u_long seqno_, 
                                       u_long send_labels_)
    : id(id_), seqno(seqno_), send_labels(send_labels_)
{
    /* empty */
}

bool 
IROBSchedulingData::operator<(const IROBSchedulingData& other) const
{
    // can implement priority here, based on 
    //  any added scheduling hints
    return (//(owner && owner->send_labels) ||
            (id < other.id) || (seqno < other.seqno));
}

void 
IROBPrioritySet::insert(IROBSchedulingData data)
{
    //TODO: do something more interesting.
    tasks.insert(data);
}

bool 
IROBPrioritySet::pop(IROBSchedulingData& data)
{
    return pop_item(tasks, data);
}
