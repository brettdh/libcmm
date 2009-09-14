#ifndef signals_h_incl
#define signals_h_incl

/* signal, fired from connection scout, used to invoke thunks */
//#define CMM_SIGNAL SIGVTALRM
#define CMM_SELECT_SIGNAL 42 /* I am assured this is okay in Linux. */

void signals_init();
void unblock_select_signals();
void block_select_signals();
void signal_selecting_threads();

#endif
