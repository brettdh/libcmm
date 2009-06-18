#ifndef thunks_h
#define thunks_h

#include <sys/types.h>
#include "libcmm.h"
#include "libcmm_ipc.h"

void enqueue_handler(mc_socket_t sock, 
		     u_long label, resume_handler_t fn, void *arg);
void print_thunks(void);
void fire_thunks(struct net_interface up_iface);
int cancel_thunk(u_long label, 
		 void (*handler)(void*), void *arg,
		 void (*deleter)(void*));

#endif
