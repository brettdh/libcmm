#ifndef csocket_sender_h_incl
#define csocket_sender_h_incl

#include "cmm_thread.h"
#include "common.h"
#include "libcmm_irob.h"
#include "pending_irob.h"
#include "cmm_socket_control.h"
#include <boost/shared_ptr.hpp>
#include "csocket.h"

class CMMSocketImpl;

class CSocketSender : public CMMThread {
  public:
    explicit CSocketSender(CSocketPtr csock_);
    
  protected:
    virtual void Run();
    virtual void Finish();
  private:
    CSocketPtr csock;
    CMMSocketImpl *sk;

    struct timespec trickle_timeout;

    // call all these below with the scheduling_state_lock held

    bool schedule_work(IROBSchedulingIndexes& indexes);
    bool delegate_if_necessary(PendingIROB *pirob, const IROBSchedulingData& data);

    bool okay_to_send_bg(struct timeval& time_since_last_fg);

    bool begin_irob(const IROBSchedulingData& data);
    void end_irob(const IROBSchedulingData& data);
    bool irob_chunk(const IROBSchedulingData& data);
    void new_interface(struct net_interface iface);
    void down_interface(struct net_interface iface);
    void send_acks(const IROBSchedulingData& data, IROBSchedulingIndexes& indexes);
    void goodbye();
};
#endif
