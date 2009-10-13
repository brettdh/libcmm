#include "timeops.h"
#include <time.h>

const long int& subseconds(const struct timeval&  tv) { return tv.tv_usec; }
const long int& subseconds(const struct timespec& tv) { return tv.tv_nsec; }
long int& subseconds(struct timeval&  tv) { return tv.tv_usec; }
long int& subseconds(struct timespec& tv) { return tv.tv_nsec; }
const long int& subseconds(const struct timeval  *tv) { return tv->tv_usec; }
const long int& subseconds(const struct timespec *tv) { return tv->tv_nsec; }
long int& subseconds(struct timeval  *tv) { return tv->tv_usec; }
long int& subseconds(struct timespec *tv) { return tv->tv_nsec; }

long int MAX_SUBSECS(const struct timeval& tv) { return 1000000; }
long int MAX_SUBSECS(const struct timeval *tv) { return 1000000; }
long int MAX_SUBSECS(const struct timespec& tv) { return 1000000000; }
long int MAX_SUBSECS(const struct timespec *tv) { return 1000000000; }

void TIME(struct timeval& tv)
{
    gettimeofday(&tv, NULL);
}

void TIME(struct timespec& tv)
{
    clock_gettime(CLOCK_REALTIME, &tv);
}


struct timespec abs_time(struct timespec rel_time)
{
    struct timespec now;
    TIME(now);
    now.tv_sec += rel_time.tv_sec;
    now.tv_nsec += rel_time.tv_nsec;
    if (now.tv_nsec >= 1000000000) {
        now.tv_sec++;
        now.tv_nsec -= 1000000000;
    }
    return now;
}
