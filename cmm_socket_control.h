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
    /* dep array follows header in network msg */
};

struct end_irob_data {
    irob_id_t id;
    ssize_t num_chunks;
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
    int numdeps;
    size_t datalen;
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
    size_t num_acks; // this many acks follow the header
    irob_id_t id; // the first of potentially many ACKs
    // (it's common for large IROBs to only send one ACK at a time)
};

struct CMMSocketControlHdr {
    short type;
    u_long send_labels;
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
  private:
    const char *type_str() const;
};

class CMMControlException : public std::runtime_error {
  public:
    CMMControlException(const std::string&, struct CMMSocketControlHdr);
    struct CMMSocketControlHdr hdr;
};

/* These being caught will cause the multi-socket connection
 * to break.  Fatal errors include unexpected control
 * messages, or invalid IROB operations, such as repeating
 * an IROB id on a connection or sending data for an IROB
 * that doesn't exist at the receiver. */
class CMMFatalError : public CMMControlException {
  public:
    CMMFatalError(const std::string& str, struct CMMSocketControlHdr hdr)
        : CMMControlException(str, hdr) {}
};

#endif
