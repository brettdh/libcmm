#ifndef cmm_socket_control_h_incl
#define cmm_socket_control_h_incl

#include "libcmm_irob.h"
#include "common.h"
#include "net_interface.h"
#include <sys/types.h>
#include <netinet/in.h>
#include <string>

#define CMM_CONTROL_MSG_HELLO          0
#define CMM_CONTROL_MSG_BEGIN_IROB     1
#define CMM_CONTROL_MSG_END_IROB       2
#define CMM_CONTROL_MSG_IROB_CHUNK     3
//#define CMM_CONTROL_MSG_DEFAULT_IROB   4
#define CMM_CONTROL_MSG_NEW_INTERFACE  5
#define CMM_CONTROL_MSG_DOWN_INTERFACE 6
#define CMM_CONTROL_MSG_ACK            7
#define CMM_CONTROL_MSG_GOODBYE        8
#define CMM_CONTROL_MSG_RESEND_REQUEST 9
#define CMM_CONTROL_MSG_DATA_CHECK     10
#define CMM_CONTROL_MSG_INVALID        11

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
    ssize_t expected_bytes;
    int expected_chunks;
};

struct irob_chunk_data {
    irob_id_t id;
    u_long seqno; /* starting at 0. */
    size_t offset; // offset in this IROB at which this chunk's data begins
    size_t datalen;
    char *data; /* NULL in network messages
                 * Allocated and used at receiver */
    /* followed by datalen bytes of application data */
};

struct SumChunkFunctor {
    size_t sum;
    SumChunkFunctor() : sum(0) {}
    void operator()(const struct irob_chunk_data& chunk) {
        sum += chunk.datalen;
    }
};

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
    // (it's common for large IROBs to only be ACK'd one at a time)

    // time from receiving all bytes to sending ACK
    struct timeval srv_time; 

    // time this ACK spent queued behind other messages; will be
    //  subtracted from RTT at receiver
    struct timeval qdelay;
};

typedef enum {
    CMM_RESEND_REQUEST_NONE = 0x0, // not used; only to complete type
    CMM_RESEND_REQUEST_DEPS = 0x1, // resend Begin_IROB msg
    CMM_RESEND_REQUEST_DATA = 0x2, // resend some chunks
    CMM_RESEND_REQUEST_END = 0x4,  // resend End_IROB msg
    CMM_RESEND_REQUEST_ALL = 0x7
} resend_request_type_t;

/* sender requesting the receiver to resend data associated
 * with this IROB. request is one of the above
 * CMM_RESEND_REQUEST_* enums; 
 *    CMM_RESEND_REQUEST_DEPS
 *       -Receiver will resend the Begin_IROB message.
 *    CMM_RESEND_REQUEST_DATA
 *       -Receiver will resend IROB_Chunk messages comprising 
 *        the IROB's data.
 *    CMM_RESEND_REQUEST_BOTH
 *       -Er, both.
 */
struct resend_request_data {
    irob_id_t id;
    resend_request_type_t request;

    // If request includes DATA, this describes the data that the receiver needs
    u_long seqno; // the seqno identifies the offset and len uniquely.
    //size_t offset;
    //size_t len;

    // If request includes END, this is the seqno of the last chunk I've received, +1.
    //  Since I didn't receive the End_IROB message, I don't know how many 
    //  chunks to expect, so tell the sender where to start.
    int next_chunk;
};

struct data_check_data {
    irob_id_t id;
};

struct CMMSocketControlHdr {
    CMMSocketControlHdr();

    short type;
    u_long send_labels;
    short msgtype() { return type; }
    void settype(short t) { type = t; }
    union {
        struct hello_data hello;
        struct begin_irob_data begin_irob;
        struct end_irob_data end_irob;
        struct irob_chunk_data irob_chunk;
        //struct default_irob_data default_irob;
        //struct new_interface_data new_interface;
        struct net_interface new_interface;
        struct down_interface_data down_interface;
        struct ack_data ack;
        struct resend_request_data resend_request;
        struct data_check_data data_check;
    } op;

    /* for use with exceptions and debug information. */
    std::string describe() const;
  private:
    const char *type_str() const;
};

class CMMControlException : public std::runtime_error {
  public:
    CMMControlException(const std::string&);
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
    CMMFatalError(const std::string& str) : CMMControlException(str) {}
    CMMFatalError(const std::string& str, struct CMMSocketControlHdr hdr)
        : CMMControlException(str, hdr) {}
};

#endif
