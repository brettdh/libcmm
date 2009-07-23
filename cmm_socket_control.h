#ifndef cmm_socket_control_h_incl
#define cmm_socket_control_h_incl

#include "libcmm_irob.h"
#include "common.h"
#include <sys/types.h>
#include <netinet/in.h>
#include <string>

#define CMM_CONTROL_MSG_HELLO          0
#define CMM_CONTROL_MSG_BEGIN_IROB     1
#define CMM_CONTROL_MSG_END_IROB       2
#define CMM_CONTROL_MSG_IROB_CHUNK     3
#define CMM_CONTROL_MSG_DEFAULT_IROB   4
#define CMM_CONTROL_MSG_NEW_INTERFACE  5
#define CMM_CONTROL_MSG_DOWN_INTERFACE 6
#define CMM_CONTROL_MSG_ACK            7
#define CMM_CONTROL_MSG_GOODBYE        8

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
    u_long seqno; /* these start at 1.  0 is invalid; see ack_data, below. */
    size_t datalen;
    char *data; /* NULL in network messages
                 * Allocated and used at receiver */
    /* followed by datalen bytes of application data */
};

struct default_irob_data {
    irob_id_t id;
    size_t datalen;
    char *data; /* NULL in network messages
                 * Allocated and used at receiver */
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
    u_long send_labels;
    u_long recv_labels;
    short msgtype() { return type; }
    void settype(short t) { type = t; }
    union {
        struct hello_data hello;
        struct begin_irob_data begin_irob;
        struct end_irob_data end_irob;
        struct irob_chunk_data irob_chunk;
	struct default_irob_data default_irob;
        struct new_interface_data new_interface;
        struct down_interface_data down_interface;
        struct ack_data ack;
    } op;

    /* for use with exceptions and debug information. */
    std::string describe() const;

    void cleanup();
  private:
    const char *type_str() const;
};

struct CMMSocketRequest {
    pthread_t requester_tid;
    struct CMMSocketControlHdr hdr;

    short msgtype() { return hdr.msgtype(); }
    void settype(short t) { hdr.settype(t); }
    std::string describe() const;
    void cleanup() { hdr.cleanup(); }
};

#endif
