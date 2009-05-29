#ifndef libcmm_constraints_incl
#define libcmm_constraints_incl

#include <libcmm.h>

typedef int chunk_id_t;

chunk_id_t begin_chunk(mc_socket_t sock, chunk_set_t dependencies);
int end_chunk(chunk_id_t);

#endif
