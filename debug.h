#ifndef debug_h_incl
#define debug_h_incl

#ifdef __cplusplus
#define CDECL extern "C"
#else
#define CDECL 
#endif

#include <pthread.h>
extern pthread_key_t thread_name_key;
#define MAX_NAME_LEN 20
char * get_thread_name();
void set_thread_name(const char *name);

CDECL void dbgprintf_always(const char *fmt, ...)
    __attribute__((format(printf, 1, 2)));

#ifdef CMM_DEBUG
CDECL bool is_debugging_on();
CDECL void set_debugging(bool value);
CDECL void dbgprintf(const char *format, ...)
  __attribute__((format(printf, 1, 2)));
CDECL void dbgprintf_plain(const char *format, ...)
  __attribute__((format(printf, 1, 2)));
#else
#define is_debugging_on() (0)
#define dbgprintf(...)
#define dbgprintf_plain(...)
#endif

#endif
