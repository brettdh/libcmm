#include "cmm_socket.h"
#include "cmm_socket.private.h"

ssize_t 
CMMSocketPassThrough::mc_send(const void *buf, size_t len, int flags,
                              u_long labels, void (*resume_handler)(void*), 
                              void *arg)
{
    return send(sock, buf, len, flags);
}

/* ...and so forth. */
