#ifndef __COMDEFS_H__
#define __COMDEFS_H__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>

#undef TRUE
#undef FALSE

#if !defined(__cplusplus)
#ifndef bool
typedef int bool;
#endif
#endif

#define TRUE 1
#define FALSE 0

// Apple definitions
#ifdef __DARWIN_UNIX03
#define EDDA_OFF_T off_t
#define EDDA_LSEEK lseek
#define EDDA_FTRUNCATE ftruncate
#define O_LARGEFILE 0
#else
#define EDDA_OFF_T loff_t
#define EDDA_LSEEK lseek64
#define EDDA_FTRUNCATE ftruncate64
#endif

typedef unsigned long magic_t;

#ifdef __STDC__

typedef long (*COMPFN)(void*,void*);  /* used by data structure routines */

#else /* __STDC__ */

typedef int (*COMPFN)();

#endif /* __STDC__ */

#define ASSERT(cond) 						      \
do {                                                                  \
    if (!(cond)) {                                                    \
	int *j = 0;                                                   \
	fprintf(stderr, "Assert (%s) in %s:%d\n",                     \
		#cond, __FILE__, __LINE__);                           \
	fflush(stderr);                                               \
	*j = 1; /* cause a real SIGTRAP to happen: can be caught */   \
    }                                                                 \
} while (0)

/*************************************** Allocation, deallocation, validity */

#ifdef ALLOC_DEBUG
#define DEBUG_ALLOC(X,S,F,L) debug_alloc(X,S,F,L)
#define DEBUG_FREE(X) debug_free (X)
#else
#define DEBUG_ALLOC(X,S,F,L)
#define DEBUG_FREE(X)
#endif

#define ALLOC(X,T)                                                \
do {                                                              \
    if (((X) = (T *)malloc(sizeof(T))) == NULL)                   \
	ASSERT(0);                                                \
    DEBUG_ALLOC (X,sizeof(T),__FILE__,__LINE__);                  \
} while (0)

#define ZALLOC(X,T)                                               \
do {                                                              \
    if (((X) = (T *)calloc(1, sizeof(T))) == NULL)                \
	ASSERT(0);                                                \
    DEBUG_ALLOC (X,sizeof(T),__FILE__,__LINE__);                  \
} while (0)

#define NALLOC(X,T,S)                                  \
do {                                                   \
    if (((X) = (T *)malloc(sizeof(T)*(S))) == NULL) {  \
	ASSERT(0);                                     \
    }                                                  \
    DEBUG_ALLOC (X,S*sizeof(T),__FILE__,__LINE__);       \
} while (0)

#define ZNALLOC(X,T,S)                                 \
do {                                                   \
    if (((X) = (T *)calloc((S),sizeof(T))) == NULL) {  \
	ASSERT(0);                                     \
    }                                                  \
    DEBUG_ALLOC (X,S*sizeof(T),__FILE__,__LINE__);       \
} while (0)

#define BALLOC(X,S)                                    \
do {                                                   \
    if (((X) = (void *)malloc((S))) == NULL) {         \
	ASSERT(0);                                     \
    }                                                  \
    DEBUG_ALLOC (X,(unsigned int) (S),__FILE__,__LINE__);       \
} while (0)

#define ZBALLOC(X,S)                                   \
do {                                                   \
    if (((X) = (void *)calloc((S),1)) == NULL) {       \
	ASSERT(0);                                     \
    }                                                  \
    DEBUG_ALLOC (X,(unsigned int) (S),__FILE__,__LINE__);       \
} while (0)

#define FREE(X)           \
do {                      \
    DEBUG_FREE (X);       \
    if ((X) != NULL) {    \
	free((X));        \
	(X) = NULL;       \
    }                     \
} while (0)

/*************************************** time operations */

/* tvp should be of type TIMEVAL */
#define TIME(tv)    (gettimeofday(&(tv),NULL))

/* tvb, tve, tvr should be of type TIMEVAL */
/* tve should be >= tvb. */
#define TIMEDIFF(tvb,tve,tvr)                                    \
do {                                                             \
    ASSERT(((tve).tv_sec > (tvb).tv_sec)                         \
	   || (((tve).tv_sec == (tvb).tv_sec)                    \
	       && ((tve).tv_usec > (tvb).tv_usec)));             \
    if ((tve).tv_usec < (tvb).tv_usec) {                         \
	(tvr).tv_usec = 1000000 + (tve).tv_usec - (tvb).tv_usec; \
	(tvr).tv_sec = (tve).tv_sec - (tvb).tv_sec - 1;          \
    } else {                                                     \
	(tvr).tv_usec = (tve).tv_usec - (tvb).tv_usec;           \
	(tvr).tv_sec = (tve).tv_sec - (tvb).tv_sec;              \
    }                                                            \
} while (0)


/* Operations on timevals. */
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
#endif /* DEF_TIMEOPS */

/* pthread macros - these functions should never fail */
#define MXLOCK(m) ASSERT(pthread_mutex_lock(m) == 0)
#define MXUNLOCK(m) ASSERT(pthread_mutex_unlock(m) == 0)
#define CDWAIT(c,m) ASSERT(pthread_cond_wait((c),(m)) == 0)
#define CDSIGNAL(c) ASSERT(pthread_cond_broadcast(c) == 0)
#define CDBCAST(c)  ASSERT(pthread_cond_broadcast(c) == 0)

#endif
