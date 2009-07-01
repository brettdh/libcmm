#ifndef cmm_socket_sender_h_incl
#define cmm_socket_sender_h_incl

#include <pthread.h>
#include <sys/types.h>
#include "cmm_socket.private.h"
#include "cmm_socket_control.h"
#include "intset.h"
#include <map>
#include "cmm_socket_scheduler.h"

class CMMSocketSender : public CMMSocketScheduler {
  public:
    explicit CMMSocketSender(CMMSocketImpl *sk_);
    ssize_t send(const void *buf, size_t len, int flags);

    irob_id_t begin_irob(u_long send_labels, u_long recv_labels,
                         int numdeps, irob_id_t *deps);
    void end_irob(irob_id_t id);
    void irob_chunk(irob_id_t, const void *buf, size_t len, int flags);
    void new_interface(struct in_addr ip_addr, u_long labels);
    void down_interface(struct in_addr ip_addr);
    void ack(irob_id_t id, u_long seqno = INVALID_IROB_SEQNO);

  protected:
    virtual void Run();
  private:
    CMMSocketImplPtr sk;
    PendingIROBHash pending_irobs;

    /* Headers passed to these functions should have integers
     * already in network byte order. */
    void pass_to_any_worker(struct CMMSocketControlHdr hdr);
    void pass_to_worker_by_labels(struct CMMSocketControlHdr hdr);
};

#endif
