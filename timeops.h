#ifndef timeops_h_incl
#define timeops_h_incl

#include <assert.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <string>
#include "debug.h"


const suseconds_t& subseconds(const struct timeval&  tv);
const long int& subseconds(const struct timespec& tv);
suseconds_t& subseconds(struct timeval&  tv);
long int& subseconds(struct timespec& tv);
const suseconds_t& subseconds(const struct timeval  *tv);
const long int& subseconds(const struct timespec *tv);
suseconds_t& subseconds(struct timeval  *tv);
long int& subseconds(struct timespec *tv);

suseconds_t MAX_SUBSECS(const struct timeval& tv);
suseconds_t MAX_SUBSECS(const struct timeval *tv);
long int MAX_SUBSECS(const struct timespec& tv);
long int MAX_SUBSECS(const struct timespec *tv);

/*************************************** time operations */

/* tvp should be of type TIMEVAL */
//#define TIME(tv)    (gettimeofday(&(tv),NULL)
void TIME(struct timeval& tv);
void TIME(struct timespec& tv);

/* tvb, tve, tvr should be of type TIMEVAL */
/* tve should be >= tvb. */
#define TIMEDIFF(tvb,tve,tvr)                                    \
do {                                                             \
    ASSERT(((tve).tv_sec > (tvb).tv_sec)                         \
           || (((tve).tv_sec == (tvb).tv_sec)                    \
               && (subseconds(tve) >= subseconds(tvb))));             \
    if (subseconds(tve) < subseconds(tvb)) {                         \
        subseconds(tvr) = MAX_SUBSECS(tvr) + subseconds(tve) - subseconds(tvb); \
        (tvr).tv_sec = (tve).tv_sec - (tvb).tv_sec - 1;          \
    } else {                                                     \
        subseconds(tvr) = subseconds(tve) - subseconds(tvb);           \
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
#undef timerdiv

#define timerclear(tvp)         (tvp)->tv_sec = subseconds(tvp) = 0
#define timerisset(tvp)         ((tvp)->tv_sec || subseconds(tvp))
#define timercmp(tvp, uvp, cmp)                                         \
        (((tvp)->tv_sec == (uvp)->tv_sec) ?                             \
            (subseconds(tvp) cmp subseconds(uvp)) :                       \
            ((tvp)->tv_sec cmp (uvp)->tv_sec))
#define timeradd(tvp, uvp, vvp)                                         \
        do {                                                            \
                (vvp)->tv_sec = (tvp)->tv_sec + (uvp)->tv_sec;          \
                subseconds(vvp) = subseconds(tvp) + subseconds(uvp);       \
                if (subseconds(vvp) >= MAX_SUBSECS(vvp)) {              \
                        (vvp)->tv_sec++;                                \
                        subseconds(vvp) -= MAX_SUBSECS(vvp);            \
                }                                                       \
        } while (0)
#define timersub(tvp, uvp, vvp)                                         \
        do {                                                            \
                (vvp)->tv_sec = (tvp)->tv_sec - (uvp)->tv_sec;          \
                subseconds(vvp) = subseconds(*tvp) - subseconds(uvp);       \
                if (subseconds(vvp) < 0) {                               \
                        (vvp)->tv_sec--;                                \
                        subseconds(vvp) += MAX_SUBSECS(vvp);            \
                }                                                       \
        } while (0)

// avg = total/n, crudely and with rounding error
#define timerdiv(total, n, avg)                                         \
    do {                                                                \
        double avg_f = ((double)(total)->tv_sec +                       \
                        ((double)subseconds(total)                      \
                         / MAX_SUBSECS(total))) / n;                    \
        (avg)->tv_sec = (time_t)avg_f;                                  \
        subseconds(avg) = (long int)((avg_f - (double)(avg)->tv_sec)    \
                                     * MAX_SUBSECS(avg));               \
    } while (0)

struct timespec abs_time(struct timespec rel_time);

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
        dbgprintf("%s took %lu.%06lu seconds\n", str,
                  diff.tv_sec, diff.tv_usec);
    }
#endif
};
#endif /* DEF_TIMEOPS */

/* convert between u_long and struct timeval */
suseconds_t convert_to_useconds(struct timeval tv);
struct timeval convert_to_timeval(u_long useconds);

#endif /* timeops_h_incl */
