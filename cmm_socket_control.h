#ifndef cmm_socket_control_h_incl
#define cmm_socket_control_h_incl

enum ControlMsgType {
    CMM_CONTROL_MSG_BEGIN_IROB,
    CMM_CONTROL_MSG_END_IROB,
    CMM_CONTROL_MSG_IROB_CHUNK,
    CMM_CONTROL_MSG_NEW_INTERFACE,
    CMM_CONTROL_MSG_DOWN_INTERFACE,
    CMM_CONTROL_MSG_ACK
};

struct begin_irob_data {
    irob_id_t id;
    u_long send_labels;
    u_long recv_labels;
    int numdeps;
    irob_id_t *deps; /* NULL in network messages
                      * Allocated and used at receiver */
    /* dep array follows header in network msg */
};

struct end_irob_data {
    irob_id_t id;
};

struct irob_chunk_data {
    irob_id_t id;
    u_long seqno;
    int datalen;
    char *data; /* NULL in network messages
                 * Allocaetd and used at receiver */
    /* followed by datalen bytes of application data */
};

struct new_interface_data {
    struct in_addr ip_addr;
    u_long labels;
};

struct down_interface_data {
    struct in_addr ip_addr;
};

enum ACKType {
    ACK_TYPE_IROB,
    ACK_TYPE_CHUNK
};

struct ack_data {
    short type;
    irob_id_t id;
    u_long seqno;
};

struct CMMSocketControlHdr {
    short type;
    union {
        struct begin_irob_data begin_irob;
        struct end_irob_data end_irob;
        struct irob_chunk_data irob_chunk;
        struct new_interface_data new_interface;
        struct down_interface_data down_interface;
        struct ack_data ack;
    } op;

    std::string describe() const;
};

#include <stdexcept>

/* These shouldn't ever be thrown in normal operation, when there's 
 * a multi-socket on each end and the two are working in harmony. 
 * When something is amiss, though, this should help to discover
 * the problem. */
class CMMControlException : public std::runtime_error {
  public:
    CMMControlException(const std::string&, struct CMMSocketControlHdr);
    struct CMMSocketControlHdr hdr;
};

#endif
