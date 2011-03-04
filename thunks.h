#ifndef thunks_h
#define thunks_h

#include <sys/types.h>
#include "libcmm.h"
#include "common.h"

void enqueue_handler(mc_socket_t sock, u_long send_labels, 
                     resume_handler_t fn, void *arg);
void print_thunks(void);
void fire_thunks(void);
int count_thunks(mc_socket_t sock);
void cancel_all_thunks(mc_socket_t sock);
int cancel_thunk(mc_socket_t sock, u_long send_label, 
                 resume_handler_t handler, void *arg,
                 void (*deleter)(void*));

#endif
