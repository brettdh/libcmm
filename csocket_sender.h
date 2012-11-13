#ifndef csocket_sender_h_incl
#define csocket_sender_h_incl

#include "cmm_thread.h"
#include "common.h"
#include "net_interface.h"
#include "libcmm_irob.h"
#include "pending_irob.h"
#include "cmm_socket_control.h"
#include <boost/shared_ptr.hpp>
#include "csocket.h"
#include <vector>
#include "pending_sender_irob.h"

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
    bool delegate_if_necessary(irob_id_t id, PendingIROBPtr& pirob,
                               const IROBSchedulingData& data);

    bool okay_to_send_bg(ssize_t& chunksize);

    bool begin_irob(const IROBSchedulingData& data);
    void end_irob(const IROBSchedulingData& data);
    bool irob_chunk(const IROBSchedulingData& data,
                    irob_id_t waiting_ack_irob);
    void new_interface(struct net_interface iface);
    void down_interface(struct net_interface iface);
    void send_acks(const IROBSchedulingData& data, IROBSchedulingIndexes& indexes);
    void goodbye();
    void resend_request(const IROBSchedulingData& data);
    void send_data_check(const IROBSchedulingData& data);

    bool nothingToSend();

    struct DataInFlight {
        bool data_inflight;
        struct timespec rel_trouble_timeout;
        DataInFlight();
        int operator()(CSocketPtr csock);
    };

    /*
    struct TroubleChecker {
        CMMSocketImpl *sk;
        std::vector<struct net_interface> troubled_ifaces;
        TroubleChecker(CMMSocketImpl *skp) : sk(skp) {}
        
        int operator()(CSocketPtr csock);
    };
    */
    
};
#endif
