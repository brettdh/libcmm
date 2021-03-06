#include "cmm_socket_control.h"
#include "debug.h"
#include <stdexcept>
#include <iostream>
#include <sstream>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "libcmm_net_restriction.h"
#include "redundancy_strategy.h"

#include <iomanip>
using std::setfill; using std::setw;

CMMSocketControlHdr::CMMSocketControlHdr()
{
    memset(this, 0, sizeof(*this));
    type = htons(CMM_CONTROL_MSG_INVALID);
}

const char *
CMMSocketControlHdr::type_str() const
{
    static const char *strs[] = {
        "Hello",
        "Begin_IROB",
        "End_IROB",
        "IROB_chunk",
        "(unknown)",//"Default IROB",
        "New_Interface",
        "Down_Interface",
        "Ack",
        "Goodbye",
        "Resend_Request",
        "Data_Check",
        "(unknown)"
    };

    short my_type = ntohs(type);
    if (my_type >= CMM_CONTROL_MSG_INVALID || my_type < CMM_CONTROL_MSG_HELLO) {
        my_type = CMM_CONTROL_MSG_INVALID;
    }
    return strs[my_type];
}

bool
irob_chunk_data::operator<(const struct irob_chunk_data& other) const
{
    return (id < other.id ||
            (id == other.id && seqno < other.seqno));
}

std::string
CMMSocketControlHdr::labels_str() const
{
    return describe_labels(ntohl(send_labels));
}

std::string
CMMSocketControlHdr::resend_request_type_str() const
{
    static const char *strs[] = {
        "deps", "data", "end"
    };

    std::ostringstream msg;
    int request = ntohl(op.resend_request.request);
    for (int i = 0; i < 3; ++i) {
        int bitmask = 1 << i;
        request = modify_bits_string(request, bitmask, strs[i], msg);
    }
    return msg.str();
}

static const char *nodebug_description = "(no debugging)";

std::string
CMMSocketControlHdr::describe() const
{
    if (!is_debugging_on()) {
        return nodebug_description;
    }

    std::ostringstream stream;
    stream << " Type: " << type_str() << "("  << ntohs(type) << ") ";
    
    stream << "Send labels: " << labels_str() << " ";

    switch (ntohs(type)) {
    case CMM_CONTROL_MSG_HELLO: {
        int type = ntohl(op.hello.redundancy_strategy_type);
        stream << "listen port: " << ntohs(op.hello.listen_port) << " ";
        stream << "num_ifaces: " << ntohl(op.hello.num_ifaces) << " ";
        stream << "redundancy_strategy_type: "
               << RedundancyStrategy::describe_type(type);
        break;
    }
    case CMM_CONTROL_MSG_BEGIN_IROB:
        stream << "IROB: " << ntohl(op.begin_irob.id) << " ";
        stream << "numdeps: " << ntohl(op.begin_irob.numdeps);
        break;
    case CMM_CONTROL_MSG_END_IROB:
        stream << "IROB: " << ntohl(op.end_irob.id) << " ";
        stream << "expected_bytes: " << ntohl(op.end_irob.expected_bytes) << " ";
        stream << "expected_chunks: " << ntohl(op.end_irob.expected_chunks);
        break;
    case CMM_CONTROL_MSG_IROB_CHUNK:
        stream << "IROB: " << ntohl(op.irob_chunk.id) << " ";
        stream << "seqno: " << ntohl(op.irob_chunk.seqno) << " ";
        stream << "offset: " << ntohl(op.irob_chunk.offset) << " ";
        stream << "datalen: " << ntohl(op.irob_chunk.datalen);
        break;
    case CMM_CONTROL_MSG_NEW_INTERFACE:
        stream << "IP: " << StringifyIP(&op.new_interface.ip_addr).c_str() << " ";
        stream << "bandwidth_down: " << ntohl(op.new_interface.bandwidth_down) << " bytes/sec, ";
        stream << "bandwidth_up: " << ntohl(op.new_interface.bandwidth_up) << " bytes/sec, ";
        stream << "RTT: " << ntohl(op.new_interface.RTT) << " ms, ";
        stream << "type: " << ntohl(op.new_interface.type);
        break;
    case CMM_CONTROL_MSG_DOWN_INTERFACE:
        stream << "IP: " << StringifyIP(&op.down_interface.ip_addr).c_str();        
        break;
    case CMM_CONTROL_MSG_ACK:
        stream << "num_acks: " << ntohl(op.ack.num_acks) << " ";
        stream << "IROB: " << ntohl(op.ack.id) << " ";
        stream << "srv_time: ";
        if (ntohl(op.ack.srv_time.tv_usec == -1)) {
            stream << "(invalid) ";
        } else {
            stream << ntohl(op.ack.srv_time.tv_sec) << "." 
                   << setfill('0') << setw(6) 
                   << ntohl(op.ack.srv_time.tv_usec) << " ";
        }
        stream << "qdelay: " << ntohl(op.ack.qdelay.tv_sec) << "." 
               << setfill('0') << setw(6) 
               << ntohl(op.ack.qdelay.tv_usec);
        break;
    case CMM_CONTROL_MSG_GOODBYE:
        break;
    case CMM_CONTROL_MSG_RESEND_REQUEST:
        stream << "IROB: " << ntohl(op.resend_request.id) << " ";
        stream << "request: " 
               << resend_request_type_str() << " ";
        stream << "seqno: " << ntohl(op.resend_request.seqno) << " ";
        stream << "next_chunk: " << ntohl(op.resend_request.next_chunk);
        break;
    case CMM_CONTROL_MSG_DATA_CHECK:
        stream << "IROB: " << ntohl(op.data_check.id);
        break;
    default:
        break;
    };
    return stream.str();
}

CMMControlException::CMMControlException(const std::string& str)
    : std::runtime_error(str)
{
    /* empty */
}

CMMControlException::CMMControlException(const std::string& str, 
                                         struct CMMSocketControlHdr hdr_)
  : std::runtime_error(str + hdr_.describe()), hdr(hdr_)
{
    /* empty */
}
