#ifndef pending_irob_h_incl
#define pending_irob_h_incl

#include <queue>
#include <set>
#include "cmm_socket_control.h"

class PendingIROB {
  public:
    PendingIROB(
    bool is_complete(void);
    bool is_released(void);
  private:
    irob_id_t id;
    u_long send_labels;
    u_long recv_labels;

    std::set<irob_id_t> deps;
    std::queue<struct irob_chunk_data> chunks;
    
    bool complete; /* true iff all of this IROB's data has been received. */
    bool released; /* true iff this IROB is complete and its dependencies 
                    * are satisfied (received by the application) */
};

#endif
