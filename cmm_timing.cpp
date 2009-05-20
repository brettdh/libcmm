#include "cmm_timing.h"

#ifdef CMM_TIMING
#include <stdio.h>
#include <sys/time.h>
#include <time.h>

#include "tbb/mutex.h"
tbb::mutex timing_mutex;

FILE *timing_file;
int num_switches;
int num_switches_to_bg;
int num_switches_to_fg;
struct timeval total_switch_time;
struct timeval total_switch_time_to_bg;
struct timeval total_switch_time_to_fg;

struct timeval total_time_in_connect;
struct timeval total_time_in_up_cb;
#endif
