#ifndef debug_h_incl
#define debug_h_incl

#ifdef __cplusplus
#define CDECL extern "C"
#else
#define CDECL 
#endif

#ifdef CMM_DEBUG
CDECL void set_debugging(bool value);
CDECL void dbgprintf(const char *format, ...)
  __attribute__((format(printf, 1, 2)));
CDECL void dbgprintf_plain(const char *format, ...)
  __attribute__((format(printf, 1, 2)));
#else
#define dbgprintf(...)
#define dbgprintf_plain(...)
#endif

#endif
