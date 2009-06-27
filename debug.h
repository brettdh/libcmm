#ifndef debug_h_incl
#define debug_h_incl

#ifdef __cplusplus
#define CDECL extern "C"
#else
#define CDECL 
#endif

#define CMM_DEBUG
#ifdef CMM_DEBUG
CDECL void dbgprintf(char *format, ...);
#else
#define dbgprintf(...)
#endif

#endif
