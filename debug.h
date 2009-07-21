#ifndef debug_h_incl
#define debug_h_incl

#ifdef __cplusplus
#define CDECL extern "C"
#else
#define CDECL 
#endif

#ifdef CMM_DEBUG
CDECL void dbgprintf(const char *format, ...);
#else
#define dbgprintf(...)
#endif

#endif
