#ifndef timeops_h_incl
#define timeops_h_incl

#include <assert.h>
#include <sys/time.h>
#include <time.h>
#include <string>
#include "debug.h"

/*************************************** time operations */

/* tvp should be of type TIMEVAL */
#define TIME(tv)    (gettimeofday(&(tv),NULL))

/* tvb, tve, tvr should be of type TIMEVAL */
/* tve should be >= tvb. */
#define TIMEDIFF(tvb,tve,tvr)                                    \
do {                                                             \
    assert(((tve).tv_sec > (tvb).tv_sec)                         \
	   || (((tve).tv_sec == (tvb).tv_sec)                    \
	       && ((tve).tv_usec >= (tvb).tv_usec)));             \
    if ((tve).tv_usec < (tvb).tv_usec) {                         \
	(tvr).tv_usec = 1000000 + (tve).tv_usec - (tvb).tv_usec; \
	(tvr).tv_sec = (tve).tv_sec - (tvb).tv_sec - 1;          \
    } else {                                                     \
	(tvr).tv_usec = (tve).tv_usec - (tvb).tv_usec;           \
	(tvr).tv_sec = (tve).tv_sec - (tvb).tv_sec;              \
    }                                                            \
} while (0)


/* Operations on timevals. */
#define DEF_TIMEOPS
#ifdef DEF_TIMEOPS
#undef timerclear
#undef timerisset
#undef timercmp
#undef timeradd
#undef timersub

#define timerclear(tvp)         (tvp)->tv_sec = (tvp)->tv_usec = 0
#define timerisset(tvp)         ((tvp)->tv_sec || (tvp)->tv_usec)
#define timercmp(tvp, uvp, cmp)                                         \
        (((tvp)->tv_sec == (uvp)->tv_sec) ?                             \
            ((tvp)->tv_usec cmp (uvp)->tv_usec) :                       \
            ((tvp)->tv_sec cmp (uvp)->tv_sec))
#define timeradd(tvp, uvp, vvp)                                         \
        do {                                                            \
                (vvp)->tv_sec = (tvp)->tv_sec + (uvp)->tv_sec;          \
                (vvp)->tv_usec = (tvp)->tv_usec + (uvp)->tv_usec;       \
                if ((vvp)->tv_usec >= 1000000) {                        \
                        (vvp)->tv_sec++;                                \
                        (vvp)->tv_usec -= 1000000;                      \
                }                                                       \
        } while (0)
#define timersub(tvp, uvp, vvp)                                         \
        do {                                                            \
                (vvp)->tv_sec = (tvp)->tv_sec - (uvp)->tv_sec;          \
                (vvp)->tv_usec = (tvp)->tv_usec - (uvp)->tv_usec;       \
                if ((vvp)->tv_usec < 0) {                               \
                        (vvp)->tv_sec--;                                \
                        (vvp)->tv_usec += 1000000;                      \
                }                                                       \
        } while (0)

struct TimeFunctionBody {
#ifndef CMM_DEBUG
    TimeFunctionBody(const char *str) { (void)str; }
#else
    struct timeval begin, end, diff;
    const char *str;
  
    TimeFunctionBody(const char *str_) : str(str_) { 
	TIME(begin); 
    }
    ~TimeFunctionBody() {
	TIME(end);
	TIMEDIFF(begin, end, diff);
	dbgprintf("%s took %lu.%06lu seconds\n", str.c_str(),
		  diff.tv_sec, diff.tv_usec);
    }
#endif
};
#endif /* DEF_TIMEOPS */

#endif /* timeops_h_incl */
