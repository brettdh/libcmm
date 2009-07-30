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

    /* These functions (begin_irob through goodbye) are called
     * by other parts of the library in order to request 
     * actions that result in network messages, storing the
     * neceessary IROB and interface data. */

    int begin_irob(irob_id_t next_irob, 
                   int numdeps, const irob_id_t *deps,
                   u_long send_labels, u_long recv_labels,
                   resume_handler_t resume_handler, void *rh_arg);
    int end_irob(irob_id_t id);
    long irob_chunk(irob_id_t, const void *buf, size_t len, int flags);
    int default_irob(irob_id_t next_irob, 
		     const void *buf, size_t len, int flags,
		     u_long send_labels, u_long recv_labels,
		     resume_handler_t resume_handler, void *arg);
    void new_interface(struct in_addr ip_addr, u_long labels);
    void down_interface(struct in_addr ip_addr);
    void ack(irob_id_t id, u_long seqno, 
	     u_long ack_send_labels, u_long ack_recv_labels);
    void goodbye(bool remote_initiated);

    /* These are called by the receiver when their associated messages
     * are received. */
    void ack_received(irob_id_t id, u_long seqno);
    void goodbye_acked(void);

    bool is_shutting_down(void);
  private:
    CMMSocketImpl *sk;

    PendingIROBLattice pending_irobs;
    void remove_if_unneeded(PendingIROBHash::accessor& ac);

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

    /* atomically enqueue the request and wait for the result. */
    /* returns result of the operation, or -1 on error */
    long enqueue_and_wait_for_completion(CMMSocketRequest req);

    void signal_completion(pthread_t requester_tid, long result);

    friend class CSocketSender;
};

#endif
