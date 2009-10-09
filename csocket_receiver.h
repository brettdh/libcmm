#ifndef csocket_receiver_h_incl
#define csocket_receiver_h_incl

#include "cmm_thread.h"
#include "cmm_socket_control.h"
#include "csocket.h"

//class CSocket;
class CMMSocketImpl;

class CSocketReceiver : public CMMThread {
  public:
    explicit CSocketReceiver(CSocketPtr csock_);

  protected:
    virtual void Run();
    virtual void Finish();

    void dispatch(struct CMMSocketControlHdr hdr);
  private:
    CSocketPtr csock;
    CMMSocketImpl *sk;

    void unrecognized_control_msg(struct CMMSocketControlHdr hdr);
    void do_begin_irob(struct CMMSocketControlHdr hdr);
    void do_end_irob(struct CMMSocketControlHdr hdr);
    void do_irob_chunk(struct CMMSocketControlHdr hdr);
    //void do_default_irob(struct CMMSocketControlHdr hdr);
    void do_new_interface(struct CMMSocketControlHdr hdr);
    void do_down_interface(struct CMMSocketControlHdr hdr);
    void do_ack(struct CMMSocketControlHdr hdr);
    void do_goodbye(struct CMMSocketControlHdr hdr);
    void do_request_resend(struct CMMSocketControlHdr hdr);

    typedef void (CSocketReceiver::*handler_fn_t)(struct CMMSocketControlHdr);

    static handler_fn_t handlers[];
};

#endif
