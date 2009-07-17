#ifndef cmm_socket_receiver_h_incl
#define cmm_socket_receiver_h_incl

#include <pthread.h>
#include <sys/types.h>
#include "cmm_socket.private.h"
#include "cmm_socket_control.h"
#include "cmm_socket_scheduler.h"
#include "intset.h"
#include <map>

#include "tbb/concurrent_hash_map.h"

#include "pending_receiver_irob.h"

class CMMSocketReceiver : public CMMSocketScheduler<struct CMMSocketControlHdr> {
  public:
    explicit CMMSocketReceiver(CMMSocketImpl *sk_);
    virtual ~CMMSocketReceiver();

    /* This function is not thread-safe, meaning (just like for
     * regular sockets) concurrent read/recv/etc. on a multi-socket
     * is not safe either. */
    ssize_t recv(void *buf, size_t len, int flags, u_long *recv_labels);

    bool find_irob(PendingIROBHash::const_accessor& ac, irob_id_t id);
  protected:
    virtual void dispatch(struct CMMSocketControlHdr hdr);
  private:
    CMMSocketImpl *sk;

    PendingReceiverIROBLattice pending_irobs;

    void do_begin_irob(struct CMMSocketControlHdr hdr);
    void do_end_irob(struct CMMSocketControlHdr hdr);
    void do_irob_chunk(struct CMMSocketControlHdr hdr);
    void do_new_interface(struct CMMSocketControlHdr hdr);
    void do_down_interface(struct CMMSocketControlHdr hdr);
    void do_ack(struct CMMSocketControlHdr hdr);
    void do_goodbye(struct CMMSocketControlHdr hdr);
};

#endif
