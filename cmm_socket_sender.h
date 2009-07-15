#ifndef cmm_socket_sender_h_incl
#define cmm_socket_sender_h_incl

#include <pthread.h>
#include <sys/types.h>
#include "cmm_socket.private.h"
#include "cmm_socket_control.h"
#include "intset.h"
#include <map>
#include "cmm_socket_scheduler.h"

class CMMSocketSender : public CMMSocketScheduler<struct CMMSocketRequest> {
  public:
    explicit CMMSocketSender(CMMSocketImpl *sk_);
    virtual ~CMMSocketSender();

    ssize_t send(const void *buf, size_t len, int flags);

    int begin_irob(irob_id_t next_irob, 
                   int numdeps, const irob_id_t *deps,
                   u_long send_labels, u_long recv_labels,
                   resume_handler_t resume_handler, void *rh_arg);
    int end_irob(irob_id_t id);
    long irob_chunk(irob_id_t, const void *buf, size_t len, int flags);
    void new_interface(struct in_addr ip_addr, u_long labels);
    void down_interface(struct in_addr ip_addr);
    void ack(irob_id_t id, u_long seqno = INVALID_IROB_SEQNO);
    void goodbye(void);

    void ack_received(irob_id_t id, u_long seqno);
    void goodbye_acked(void);

    bool is_shutting_down(void);
  private:
    CMMSocketImpl *sk;

    PendingIROBLattice pending_irobs;

    std::map<pthread_t, AppThread> app_threads;

    /* true iff the socket has begun shutting down 
     * via shutdown() or close(). */
    bool shutting_down;
    bool remote_shutdown; /* true if remote has ack'd the shutdown */
    pthread_mutex_t shutdown_mutex;
    pthread_cond_t shutdown_cv;

    /* Headers passed to these functions should have integers
     * already in network byte order. */
    void pass_to_any_worker(struct CMMSocketRequest req);
    void pass_to_worker_by_labels(struct CMMSocketRequest req);
    void pass_to_any_worker_prefer_labels(struct CMMSocketRequest req);

    /* the wait_for_completion functions will atomically enqueue 
     * the request and begin waiting for the result. */

    /* returns result of underlying send, or -1 on error */
    long enqueue_and_wait_for_completion(CMMSocketRequest req);

    void signal_completion(pthread_t requester_tid, long result);

    friend class CSocketSender;
};

#endif
