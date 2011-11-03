#ifndef LIBCMM_PRIVATE_H
#define LIBCMM_PRIVATE_H

/* Private functions useful for testing.
   Not to be exported to applications. */

#include "libcmm_irob.h"

#ifdef __cplusplus
extern "C" {
#endif

void CMM_PRIVATE_drop_irob_and_dependents(irob_id_t irob);

#ifdef __cplusplus
}
#endif

#endif /* LIBCMM_PRIVATE_H */
