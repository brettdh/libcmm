#ifndef csocket_sender_h_incl
#define csocket_sender_h_incl

#include "cmm_thread.h"
#include "common.h"
#include "libcmm_irob.h"
#include "pending_irob.h"
#include "cmm_socket_control.h"

class CSocket;

class CSocketSender : public CMMThread {
  public:
    explicit CSocketSender(CSocket *csock_);
    
  protected:
    virtual void Run();
    virtual void Finish();
  private:
    CSocket *csock;

    // call all these below with the scheduling_state_lock held

    bool schedule_on_my_labels();
    bool schedule_unlabeled();
    bool schedule_work(IROBSchedulingIndexes& indexes);
    bool delegate_if_necessary(PendingIROB *pirob, const IROBSchedulingData& data);

    void begin_irob(const IROBSchedulingData& data);
    void end_irob(const IROBSchedulingData& data);
    void irob_chunk(const IROBSchedulingData& data);
    void new_interface(struct net_interface iface);
    void down_interface(struct net_interface iface);
    void ack(const IROBSchedulingData& data);
    void goodbye();
};
#endif
