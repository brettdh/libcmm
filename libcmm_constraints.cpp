#include "libcmm_constraints.h"

substream_id_t begin_substream(mc_socket_t sock, 
                               int depnum, substream_set_t *dependencies)
{
    
}

int end_substream(substream_id_t substream)
{
    
}

ssize_t substream_send(substream_id_t substream, 
                       const void *buf, size_t len, int flags,
                       u_long labels, resume_handler_t handler, void *arg)
{
    
}

int substream_writev(substream_id_t substream, 
                     const struct iovec *vector, int count,
                     u_long labels, resume_handler_t handler, void *arg)
{
    
}
