#ifndef pending_irob_h_incl
#define pending_irob_h_incl

#include <queue>
#include <set>
#include "libcmm.h"
#include "cmm_socket_control.h"
#include "cmm_socket.private.h"
#include "intset.h"
#include "pending_irob.h"

typedef std::unary_function<irob_id_t, bool> Predicate;

class PendingReceiverIROB : public PendingIROB {
  public:
    PendingReceiverIROB(struct begin_irob_data data);
    
    void release_dependents();

    /* have all the deps been satisfied? 
     * (only meaningful on the receiver side) */
    bool is_released(void);

  private:
    friend class CMMSocketReceiver;

    ssize_t bytes_read;
};

#endif
