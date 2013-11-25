#ifndef debug_h_incl
#define debug_h_incl

#include <unistd.h>

#include <pthread.h>
extern pthread_key_t thread_name_key;
#define MAX_NAME_LEN 20
char * get_thread_name();
void set_thread_name(const char *name);

void dbgprintf_always(const char *fmt, ...)
    __attribute__((format(printf, 1, 2)));

#ifdef CMM_DEBUG
bool is_debugging_on();
void dbgprintf(const char *format, ...)
  __attribute__((format(printf, 1, 2)));
void dbgprintf_plain(const char *format, ...)
  __attribute__((format(printf, 1, 2)));
#else
#define is_debugging_on(...) (0)
#define dbgprintf(...)
#define dbgprintf_plain(...)
#endif

#define ASSERT(cond)                                                    \
    do {                                                                \
        if (!(cond)) {                                                  \
            dbgprintf_always("ASSERT '" #cond "' failed at %s:%d\n", __FILE__, __LINE__); \
            __builtin_trap();                                          \
        }                                                              \
    } while (0)

#ifdef __cplusplus
#include <string>
namespace intnw {
    void check(bool success, const std::string& msg);
}
#endif


#endif
