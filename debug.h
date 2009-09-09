#ifndef debug_h_incl
#define debug_h_incl

#ifdef __cplusplus
#define CDECL extern "C"
#else
#define CDECL 
#endif

#ifdef CMM_DEBUG
void set_debugging(bool value);
CDECL void dbgprintf(const char *format, ...);
CDECL void dbgprintf_plain(const char *format, ...);
#else
#define dbgprintf(...)
#define dbgprintf_plain(...)
#endif

#endif
