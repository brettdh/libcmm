#include "debug.h"
#include <stdio.h>
#include <stdarg.h>
#include "timeops.h"
#include <pthread.h>
#include "cmm_thread.h"

#ifdef CMM_DEBUG
static bool debugging = true;

void set_debugging(bool value)
{
    debugging = value;
}

static void vdbgprintf(bool plain, const char *fmt, va_list ap)
{
    if (!plain) {
        struct timeval now;
        TIME(now);
        fprintf(stderr, "[%lu.%06lu][%d][%s] ",
                now.tv_sec, now.tv_usec, getpid(), 
#ifdef CMM_UNIT_TESTING
                "(unit testing)"
#else
                get_thread_name()
#endif
                );
    }
    
    vfprintf(stderr, fmt, ap);
}

void dbgprintf(const char *fmt, ...)
{
    if (debugging) {
        va_list ap;
        va_start(ap, fmt);
        vdbgprintf(false, fmt, ap);
        va_end(ap);
    }
}

void dbgprintf_plain(const char *fmt, ...)
{
    if (debugging) {
        va_list ap;
        va_start(ap, fmt);
        vdbgprintf(true, fmt, ap);
        va_end(ap);
    }
}
#endif
