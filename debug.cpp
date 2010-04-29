#include "debug.h"
#include <stdio.h>
#include <stdarg.h>
#include <errno.h>
#include <unistd.h>
#include "timeops.h"
#include <pthread.h>
#include "cmm_thread.h"
#include <sstream>
#include <string>
#include <iomanip>
using std::ostringstream; using std::string; 
using std::setw; using std::setfill;

#ifdef CMM_DEBUG
static bool debugging = true;

void set_debugging(bool value)
{
    debugging = value;
}

static void vdbgprintf(bool plain, const char *fmt, va_list ap)
{
    ostringstream stream;
    if (!plain) {
        struct timeval now;
        TIME(now);
        stream << "[" << now.tv_sec << "." << setw(6) << setfill('0') << now.tv_usec << "]";
        stream << "[" << getpid() << "]";
        stream << "[";
#ifdef CMM_UNIT_TESTING
        stream << "(unit testing)";
#else
        stream << get_thread_name();
#endif
        stream << "] ";
    }

    string fmtstr(stream.str());
    fmtstr += fmt;
    
#ifdef ANDROID
    int rc = vprintf(fmtstr.c_str(), ap);
#else
    int rc = vfprintf(stderr, fmtstr.c_str(), ap);
#endif
    if (rc <= 0) {
        rc = std::printf("vfprintf error: %s\n", strerror(errno));
    }
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
