#include "cmm_socket_control.h"
#include <stdexcept>
#include <iostream>
#include <sstream>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <iomanip>
using std::setfill; using std::setw;

CMMSocketControlHdr::CMMSocketControlHdr()
{
    memset(this, 0, sizeof(this));
    type = CMM_CONTROL_MSG_INVALID;
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

std::string
CMMSocketControlHdr::describe() const
{
    std::ostringstream stream;
    if (ntohs(type) != CMM_CONTROL_MSG_INVALID) {
        stream << " Type: " << type_str() << " ";
        stream << "Send labels: " << ntohl(send_labels) << " ";
    }

    switch (ntohs(type)) {
    case CMM_CONTROL_MSG_HELLO:
      stream << "listen port: " << ntohs(op.hello.listen_port) << " ";
      stream << "num_ifaces: " << ntohl(op.hello.num_ifaces);
      break;
    case CMM_CONTROL_MSG_BEGIN_IROB:
        stream << "IROB: " << ntohl(op.begin_irob.id) << " ";
        stream << "numdeps: " << ntohl(op.begin_irob.numdeps);
        break;
    case CMM_CONTROL_MSG_END_IROB:
      stream << "IROB: " << ntohl(op.end_irob.id) << " ";
        stream << "expected_bytes: " << ntohl(op.end_irob.expected_bytes);
        break;
    case CMM_CONTROL_MSG_IROB_CHUNK:
        stream << "IROB: " << ntohl(op.irob_chunk.id) << " ";
	stream << "seqno: " << ntohl(op.irob_chunk.seqno) << " ";
	stream << "offset: " << ntohl(op.irob_chunk.offset) << " ";
        stream << "datalen: " << ntohl(op.irob_chunk.datalen);
        break;
#if 0
    case CMM_CONTROL_MSG_DEFAULT_IROB:
        stream << "IROB: " << ntohl(op.default_irob.id) << " ";
        stream << "numdeps: " << ntohl(op.default_irob.numdeps);
        stream << "datalen: " << ntohl(op.default_irob.datalen);
	break;
#endif
    case CMM_CONTROL_MSG_NEW_INTERFACE:
        stream << "IP: " << inet_ntoa(op.new_interface.ip_addr) << " ";
        stream << "labels: " << ntohl(op.new_interface.labels) << " ";
        stream << "bandwidth: " << ntohl(op.new_interface.bandwidth) << " bytes/sec, ";
        stream << "RTT: " << ntohl(op.new_interface.RTT) << " ms";
        break;
    case CMM_CONTROL_MSG_DOWN_INTERFACE:
        stream << "IP: " << inet_ntoa(op.down_interface.ip_addr);        
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
        stream << "request: " << ntohl(op.resend_request.request) << " ";
        stream << "offset: " << ntohl(op.resend_request.offset);
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
