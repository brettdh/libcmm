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

#ifdef ANDROID
#include <cutils/logd.h>
#endif

pthread_key_t thread_name_key;
static pthread_once_t key_once = PTHREAD_ONCE_INIT;

static void delete_name_string(void *arg)
{
    char *name_str = (char*)arg;
    delete [] name_str;
}

static void make_key()
{
    (void)pthread_key_create(&thread_name_key, delete_name_string);
    pthread_setspecific(thread_name_key, NULL);
}

void set_thread_name(const char *name)
{
    (void) pthread_once(&key_once, make_key);

    assert(name);
    char *old_name = (char*)pthread_getspecific(thread_name_key);
    delete [] old_name;

    char *name_str = new char[MAX_NAME_LEN+1];
    memset(name_str, 0, MAX_NAME_LEN+1);
    strncpy(name_str, name, MAX_NAME_LEN);
    pthread_setspecific(thread_name_key, name_str);
}

char * get_thread_name()
{
    (void) pthread_once(&key_once, make_key);

    char * name_str = (char*)pthread_getspecific(thread_name_key);
    if (!name_str) {
        char *name = new char[12];
        sprintf(name, "%08lx", pthread_self());
        pthread_setspecific(thread_name_key, name);
        name_str = name;
    }

    return name_str;
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
    //int rc = vprintf(fmtstr.c_str(), ap);
    __android_log_vprint(ANDROID_LOG_INFO, "libcmm", fmtstr.c_str(), ap);
#else
    vfprintf(stderr, fmtstr.c_str(), ap);
#endif
}

void dbgprintf_always(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vdbgprintf(true, fmt, ap);
    va_end(ap);
}

#ifdef CMM_DEBUG
static bool debugging = true;

void set_debugging(bool value)
{
    debugging = value;
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
