#ifndef thunks_h
#define thunks_h

#include <sys/types.h>
#include "libcmm.h"

void enqueue_handler(mc_socket_t sock, 
		     u_long label, resume_handler_t fn, void *arg);
void print_thunks(void);
void fire_thunks(u_long cur_labels);
int cancel_thunk(u_long label, 
		 void (*handler)(void*), void *arg,
		 void (*deleter)(void*));

#endif
