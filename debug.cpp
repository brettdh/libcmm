#include "debug.h"
#include <stdio.h>
#include <stdarg.h>

#ifdef CMM_DEBUG
void dbgprintf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}
#endif
