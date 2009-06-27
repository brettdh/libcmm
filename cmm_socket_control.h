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
    in_addr_t ip_addr;
    u_long labels;
};

struct down_interface_data {
    in_addr_t ip_addr;
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
        struct hello_data hello;
        struct begin_irob_data begin_irob;
        struct end_irob_data end_irob;
        struct irob_chunk_data irob_chunk;
        struct new_interface_data new_interface;
        struct down_interface_data down_interface;
        struct ack_data ack;
    } op;
};

#endif
