#ifndef cmm_socket_receiver_h_incl
#define cmm_socket_receiver_h_incl

#include <pthread.h>
#include <sys/types.h>
#include "cmm_socket.private.h"
#include "cmm_socket_control.h"

class CMMSocketReceiver {
  public:
    ssize_t read(void *buf, size_t len);

    CMMSocketReceiver(CMMSocketImpl *sk_);
    void RunReceiver(void);
    void enqueue(struct CMMSocketMessageHdr hdr);
  private:
    pthread_t tid;
    CMMSocketImpl *sk;

    bool dispatch(struct CMMSocketControlHdr hdr);

    typedef bool (CMMSocketReceiver::*dispatch_fn_t)(struct CMMSocketControlHdr);
    static std::map<short, CMMSocketReceiver::dispatch_fn_t> dispatcher;

    bool do_begin_irob(struct CMMSocketControlHdr hdr);
    bool do_end_irob(struct CMMSocketControlHdr hdr);
    bool do_irob_chunk(struct CMMSocketControlHdr hdr);
    bool do_new_interface(struct CMMSocketControlHdr hdr);
    bool do_down_interface(struct CMMSocketControlHdr hdr);
    bool do_ack(struct CMMSocketControlHdr hdr);
    bool unrecognized_control_msg(struct CMMSocketControlHdr hdr);
};

#endif
