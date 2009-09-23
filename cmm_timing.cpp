#include "cmm_timing.h"

#ifdef CMM_TIMING
#include <stdio.h>
#include <sys/time.h>
#include <time.h>
#include <map>
using std::map;

#include <pthread.h>
#include "pthread_util.h"
pthread_mutex_t timing_mutex;

FILE *timing_file;

//GlobalStats global_stats;

GlobalStats::~GlobalStats()
{
    PthreadScopedLock lock(&timing_mutex);
    
    if (timing_file) {
        fprintf(timing_file, "Statistics for this run:\n");
        fprintf(timing_file, "%-6s %-9s %-9s %-9s %-15s %-15s %-15s\n",
                "Label", "Sends", "Recvs", "Total",
                "Bytes sent", "Bytes received", "Total");
        for (map<u_long, int>::iterator it = bytes_sent.begin();
             it != bytes_sent.end(); ++it) {
            u_long label = it->first;
            fprintf(timing_file, "%-6lu %-9d %-9d %-9d %-15d %-15d %-15d\n",
                    label, send_count[label], recv_count[label],
                    send_count[label] + recv_count[label],
                    bytes_sent[label], bytes_received[label],
                    bytes_sent[label] + bytes_received[label]);
        }
    }
}


#endif
