#include "cmm_socket_control.h"
#include <stdexcept>
#include <iostream>
#include <sstream>
#include <netinet/in.h>
#include <arpa/inet.h>

void 
CMMSocketControlHdr::cleanup()
{
    if (type == CMM_CONTROL_MSG_BEGIN_IROB) {
        delete [] op.begin_irob.deps;
    } else if (type == CMM_CONTROL_MSG_IROB_CHUNK) {
        delete [] op.irob_chunk.data;
    }
}

const char *
CMMSocketControlHdr::type_str() const
{
    static const char *strs[] = {
	"Hello",
	"Begin IROB",
	"End IROB",
	"IROB chunk",
	"New Interface",
	"Down Interface",
	"Ack",
	"(unknown)"
    };
    short my_type = ntohs(type);
    if (my_type > CMM_CONTROL_MSG_ACK || my_type < CMM_CONTROL_MSG_HELLO) {
	my_type = CMM_CONTROL_MSG_ACK + 1;
    }
    return strs[my_type];
}

std::string
CMMSocketControlHdr::describe() const
{
    std::ostringstream stream;
    stream << std:: endl << "Type: " << type_str() << " ";
    switch (ntohs(type)) {
    case CMM_CONTROL_MSG_HELLO:
      stream << "listen port: " << ntohs(op.hello.listen_port) << " ";
      stream << "num_ifaces: " << ntohl(op.hello.num_ifaces);
      break;
    case CMM_CONTROL_MSG_BEGIN_IROB:
        stream << "IROB: " << ntohl(op.begin_irob.id)<< " ";
        stream << "send_labels: " << ntohl(op.begin_irob.send_labels)<< " ";
        stream << "recv_labels: " << ntohl(op.begin_irob.recv_labels)<< " ";
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
        stream << "IROB: " << op.end_irob.id;
        break;
    case CMM_CONTROL_MSG_IROB_CHUNK:
        stream << "IROB: " << ntohl(op.irob_chunk.id)<< " ";
        stream << "seqno: " << ntohl(op.irob_chunk.seqno)<< " ";
        stream << "datalen: " << op.irob_chunk.datalen;
        break;
    case CMM_CONTROL_MSG_NEW_INTERFACE:
        stream << "IP: " << inet_ntoa(op.new_interface.ip_addr) << " ";
        stream << "labels: " << op.new_interface.labels;
        break;
    case CMM_CONTROL_MSG_DOWN_INTERFACE:
        stream << "IP: " << inet_ntoa(op.down_interface.ip_addr);        
        break;
    case CMM_CONTROL_MSG_ACK:
        stream << "IROB: " << op.ack.id;
        if (ntohl(op.ack.seqno)== INVALID_IROB_SEQNO) {
            stream << " seqno: " << op.ack.seqno;
        }
        break;
    default:
        stream << " (unknown type)";
        break;
    };
    stream << std::endl;
    return stream.str();
}

std::string
CMMSocketRequest::describe() const
{
    std::ostringstream stream;
    stream << std::endl << "Requester thread: " << requester_tid;
    return stream.str() + hdr.describe();
}
