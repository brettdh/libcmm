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

void TIME(struct timespec& ts)
{
    struct timeval tv;
    TIME(tv);
    ts.tv_sec = tv.tv_sec;
    ts.tv_nsec = tv.tv_usec * 1000;
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

suseconds_t convert_to_useconds(struct timeval tv)
{
    suseconds_t useconds = tv.tv_sec * 1000000;
    if (useconds < (suseconds_t)tv.tv_sec) {
        // overflow
        return 0;
    }
    return (useconds + tv.tv_usec);
}

struct timeval convert_to_timeval(u_long useconds)
{
    struct timeval tv;
    tv.tv_sec = useconds / 1000000;
    tv.tv_usec = useconds - (tv.tv_sec * 1000000);
    return tv;
}
