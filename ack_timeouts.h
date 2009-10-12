#ifndef ack_timeouts_h_incl
#define ack_timeouts_h_incl

#include "libcmm_irob.h"
#include <time.h>
#include <sys/time.h>
#include <vector>
#include <set>
#include <map>

class AckTimeouts {
  public:
    // insert or update a timeout (now + rel_timeout) for the given IROB.
    void update(irob_id_t id, const struct timespec& rel_timeout);

    // return, but do not remove, the timeout that will expire next, 
    //   in abs_timeout.
    // if none exists, return false, and abs_timeout is unmodified.
    // otherwise return true.
    bool get_earliest(struct timespec& abs_timeout) const;

    // return and remove IROB ids for all the expired timeouts.
    std::vector<irob_id_t> remove_expired();

    // Remove the timeout for the given IROB.
    void remove(irob_id_t id);

  private:
    struct node {
        irob_id_t id;
        struct timespec tv;

        bool operator<(const struct node& other) const;
    };

    typedef std::multiset<struct node> SetType;
    SetType timeouts;
    std::map<irob_id_t, SetType::iterator> timeouts_by_irob;
};

#endif
