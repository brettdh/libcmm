#include "debug.h"
#include <stdio.h>
#include <stdarg.h>
#include "timeops.h"
#include <pthread.h>
#include "cmm_thread.h"

#ifdef CMM_DEBUG
void dbgprintf(const char *fmt, ...)
{
    struct timeval now;
    TIME(now);
    fprintf(stderr, "[%lu.%06lu][%s] ",
	    now.tv_sec, now.tv_usec, get_thread_name());

    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}
#endif
