#ifndef LIBCMM_PRIVATE_H
#define LIBCMM_PRIVATE_H

/* Private functions useful for testing.
   Not to be exported to applications. */

#include "libcmm_irob.h"

#ifdef __cplusplus
extern "C" {
#endif

void CMM_PRIVATE_drop_irob_and_dependents(irob_id_t irob);

/* Return the number of connections currently managed by sock. 
 * This is not available to applications by default, because
 *  it exposes an underlying detail that IntNW encourages 
 *  applications to ignore.
 */
int CMM_PRIVATE_num_networks(mc_socket_t sock);

#ifdef __cplusplus
}
#endif

#endif /* LIBCMM_PRIVATE_H */
