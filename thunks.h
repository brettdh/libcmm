#ifndef thunks_h
#define thunks_h

#include <sys/types.h>
#include "libcmm.h"
#include "common.h"

void enqueue_handler(mc_socket_t sock, u_long send_labels, u_long recv_labels, 
                     resume_handler_t fn, void *arg);
void print_thunks(void);
void fire_thunks(struct net_interface up_iface);
int cancel_thunk(mc_socket_t sock, u_long send_label, u_long recv_label,
		 void (*handler)(void*), void *arg,
		 void (*deleter)(void*));

#endif
