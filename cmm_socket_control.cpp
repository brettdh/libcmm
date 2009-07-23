#include "cmm_socket_control.h"
#include <stdexcept>
#include <iostream>
#include <sstream>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "cmm_socket_scheduler.h"

void 
CMMSocketControlHdr::cleanup()
{
    if (type == CMM_CONTROL_MSG_BEGIN_IROB) {
        delete [] op.begin_irob.deps;
    } else if (type == CMM_CONTROL_MSG_IROB_CHUNK ||
	       type == CMM_CONTROL_MSG_DEFAULT_IROB) {
        delete [] op.irob_chunk.data;
    }
}

const char *
CMMSocketControlHdr::type_str() const
{
    static const char *strs[] = {
	"Hello",
	"Begin_IROB",
	"End_IROB",
	"IROB_chunk",
	"Default IROB",
	"New_Interface",
	"Down_Interface",
	"Ack",
	"Goodbye",
	"(unknown)"
    };
    static const char *term_str = "(Terminate thread)";

    short my_type = ntohs(type);
    if (my_type == CMMSocketScheduler<CMMSocketControlHdr>::CMM_TERMINATE_THREAD) {
	return term_str;
    } else if (my_type > CMM_CONTROL_MSG_GOODBYE || my_type < CMM_CONTROL_MSG_HELLO) {
	my_type = CMM_CONTROL_MSG_GOODBYE + 1;
    }
    return strs[my_type];
}

std::string
CMMSocketControlHdr::describe() const
{
    std::ostringstream stream;
    stream << "Type: " << type_str() << " ";
    if (type != CMMSocketScheduler<CMMSocketControlHdr>::CMM_TERMINATE_THREAD) {
	stream << "Send labels: " << ntohl(send_labels) << " ";
	stream << "Recv labels: " << ntohl(recv_labels) << " ";
    }
    switch (ntohs(type)) {
    case CMM_CONTROL_MSG_HELLO:
      stream << "listen port: " << ntohs(op.hello.listen_port) << " ";
      stream << "num_ifaces: " << ntohl(op.hello.num_ifaces);
      break;
    case CMM_CONTROL_MSG_BEGIN_IROB:
        stream << "IROB: " << ntohl(op.begin_irob.id) << " ";
        stream << "numdeps: " << op.begin_irob.numdeps;
        if (op.begin_irob.deps) {
            stream << " [ ";
            for (int i = 0; i < op.begin_irob.numdeps; i++) {
		stream << ntohl(op.begin_irob.deps[i]) << " ";
            }
            stream << "]";
        }
        break;
    case CMM_CONTROL_MSG_END_IROB:
        stream << "IROB: " << ntohl(op.end_irob.id);
        break;
    case CMM_CONTROL_MSG_IROB_CHUNK:
        stream << "IROB: " << ntohl(op.irob_chunk.id) << " ";
	stream << "seqno: " << ntohl(op.irob_chunk.seqno) << " ";
        stream << "datalen: " << ntohl(op.irob_chunk.datalen);
        break;
    case CMM_CONTROL_MSG_DEFAULT_IROB:
        stream << "IROB: " << ntohl(op.default_irob.id) << " ";
        stream << "datalen: " << ntohl(op.default_irob.datalen);
	break;
    case CMM_CONTROL_MSG_NEW_INTERFACE:
        stream << "IP: " << inet_ntoa(op.new_interface.ip_addr) << " ";
        stream << "labels: " << ntohl(op.new_interface.labels);
        break;
    case CMM_CONTROL_MSG_DOWN_INTERFACE:
        stream << "IP: " << inet_ntoa(op.down_interface.ip_addr);        
        break;
    case CMM_CONTROL_MSG_ACK:
        stream << "IROB: " << ntohl(op.ack.id);
        if (ntohl(op.ack.seqno) != INVALID_IROB_SEQNO) {
            stream << " seqno: " << ntohl(op.ack.seqno);
        }
        break;
    case CMM_CONTROL_MSG_GOODBYE:
	break;
    default:
        break;
    };
    return stream.str();
}

std::string
CMMSocketRequest::describe() const
{
    std::ostringstream stream;
    stream << "Requester thread: " << requester_tid << " ";
    return stream.str() + hdr.describe();
}
