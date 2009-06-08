/* IROB API sketch */

typedef int irob_id_t;

irob_id_t begin_irob(int sock, irob_id_t *deps, int depnum);
irob_id_t begin_irob_label(mc_socket_t sock, irob_id_t *deps, int depnum,
                           u_long labels, resume_handler_t fn, void *arg);

int end_irob(irob_id_t id);

ssize_t irob_send(irob_id_t irob_id, void *buf, size_t len, int flags);
int irob_writev(irob_id_t irob_id, const struct iovec *vector, int count);

/*
  Things to remember:

  1) Receiver (app) should somehow receive the sender's intent (labels).
  2) Default should be: atomic and dependent on all previous sends
     (Start correct, and then relax ordering to increase performance)
 */
