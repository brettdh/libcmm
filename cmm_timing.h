#ifndef cmm_timing_h
#define cmm_timing_h

#define CMM_TIMING
#ifdef CMM_TIMING
#include "timeops.h"
#include <map>

#include <pthread.h>
extern pthread_mutex_t timing_mutex;
#define TIMING_FILE "/tmp/cmm_timing.txt"
extern FILE *timing_file;

class GlobalStats {
  public:
    std::map<u_long, int> bytes_sent;
    std::map<u_long, int> bytes_received;
    std::map<u_long, int> send_count;
    std::map<u_long, int> recv_count;

    ~GlobalStats();
};

extern GlobalStats global_stats;
#endif


#endif /* include guard */
