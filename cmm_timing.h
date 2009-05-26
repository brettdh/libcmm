#ifndef cmm_timing_h
#define cmm_timing_h

#define CMM_TIMING
#ifdef CMM_TIMING
#include "timeops.h"

#include "tbb/mutex.h"
extern tbb::mutex timing_mutex;
#define TIMING_FILE "/tmp/cmm_timing.txt"
extern FILE *timing_file;
extern int num_switches;
extern int num_switches_to_bg;
extern int num_switches_to_fg;
extern struct timeval total_switch_time;
extern struct timeval total_switch_time_to_bg;
extern struct timeval total_switch_time_to_fg;

extern struct timeval total_time_in_connect;
extern struct timeval total_time_in_up_cb;
#endif


#endif /* include guard */
