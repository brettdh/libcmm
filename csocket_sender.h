#ifndef csocket_sender_h_incl
#define csocket_sender_h_incl

#include "cmm_thread.h"
#include "common.h"
#include "libcmm_irob.h"

class CSocket;

class CSocketSender : public CMMThread {
  public:
    explicit CSocketSender(CSocket *csock_);
    
  protected:
    virtual void Run();
    virtual void Finish();
  private:
    CSocket *csock;

    // call these with the scheduling_state_lock held
    bool schedule_on_my_labels();
    bool schedule_unlabeled();
    void begin_irob(irob_id_t id);
    void end_irob(irob_id_t id);
    void irob_chunk(irob_id_t id, u_long seqno);
    void default_irob(irob_id_t id);
    void new_interface(struct net_interface iface);
    void down_interface(struct net_interface iface);
    void ack(irob_id_t id, u_long seqno);
    void goodbye();
};

#endif
