#ifndef cmm_socket_control_h_incl
#define cmm_socket_control_h_incl

enum ControlMsgType {
    CMM_CONTROL_MSG_HELLO,
    CMM_CONTROL_MSG_BEGIN_IROB,
    CMM_CONTROL_MSG_END_IROB,
    CMM_CONTROL_MSG_IROB_CHUNK,
    CMM_CONTROL_MSG_NEW_INTERFACE,
    CMM_CONTROL_MSG_DOWN_INTERFACE,
    CMM_CONTROL_MSG_ACK
};

struct hello_data {
    in_port_t listen_port;
    int num_ifaces;
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
    u_long seqno; /* these start at 1.  0 is invalid; see ack_data, below. */
    size_t datalen;
    char *data; /* NULL in network messages
                 * Allocaetd and used at receiver */
    /* followed by datalen bytes of application data */
};

#define INVALID_IROB_SEQNO 0

struct new_interface_data {
    struct in_addr ip_addr;
    u_long labels;
};

struct down_interface_data {
    struct in_addr ip_addr;
};

struct ack_data {
    irob_id_t id;
    u_long seqno; /* since IROB seqnos start at 1, a seqno of 0 here means
                   * that the entire IROB is ACK'd. */
};

struct CMMSocketControlHdr {
    short type;
    short msgtype() { return type; }
    union {
        struct hello_data hello;
        struct begin_irob_data begin_irob;
        struct end_irob_data end_irob;
        struct irob_chunk_data irob_chunk;
        struct new_interface_data new_interface;

        struct ack_data ack;
    } op;

    /* for use with exceptions and debug information. */
    std::string describe() const;
};

struct CMMSocketRequest {
    pthread_t requester_tid;
    struct CMMSocketControlHdr hdr;

    short msgtype() { return hdr.type; }
    std::string describe() const;
};

#endif
