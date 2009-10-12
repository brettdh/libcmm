#include "ack_timeouts.h"
#include "libcmm_irob.h"
#include <time.h>
#include <sys/time.h>
#include "timeops.h"
#include <vector>
#include <map>
#include <set>
using std::vector;

bool 
AckTimeouts::node::operator<(const struct node& other) const
{
    return ((tv.tv_sec < other.tv.tv_sec) ||
            ((tv.tv_sec == other.tv.tv_sec) &&
             (tv.tv_nsec < other.tv.tv_nsec)));
}

// insert or update a timeout (now + rel_timeout) for the given IROB.
void 
AckTimeouts::update(irob_id_t id, const struct timespec& rel_timeout)
{
    if (timeouts_by_irob.find(id) != timeouts_by_irob.end()) {
        remove(id);
    }

    struct node new_tv;
    new_tv.id = id;
    TIME(new_tv.tv);

    SetType::iterator pos = timeouts.insert(new_tv);
    timeouts_by_irob[id] = pos;
}

// return, but do not remove, the timeout that will expire next, 
//   in abs_timeout.
// if none exists, return false, and abs_timeout is unmodified.
// otherwise return true.
bool 
AckTimeouts::get_earliest(struct timespec& abs_timeout) const
{
    SetType::iterator head = timeouts.begin();
    if (head != timeouts.end()) {
        abs_timeout = head->tv;
        return true;
    } else {
        return false;
    }
}

// return and remove IROB ids for all the expired timeouts.
vector<irob_id_t> 
AckTimeouts::remove_expired()
{
    struct node dummy;
    TIME(dummy.tv);
    SetType::iterator tail = timeouts.upper_bound(dummy);
    
    vector<irob_id_t> ret;
    for (SetType::iterator it = timeouts.begin(); it != tail; it++) {
        ret.push_back(it->id);
        timeouts_by_irob.erase(it->id);
    }
    timeouts.erase(timeouts.begin(), tail);
    return ret;
}

// Remove the timeout for the given IROB.
void 
AckTimeouts::remove(irob_id_t id)
{
    if (timeouts_by_irob.find(id) != timeouts_by_irob.end()) {
        SetType::iterator pos = timeouts_by_irob[id];
        timeouts.erase(pos);
        timeouts_by_irob.erase(id);
    }
}

