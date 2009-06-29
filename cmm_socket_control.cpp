#include "cmm_socket_control.h"
#include <stdexcept>

CMMControlException::CMMControlException(const std::string& str, 
                                         struct CMMSocketControlHdr hdr_)
  : std::runtime_error(str + hdr_.describe()), hdr(hdr_)
{
    /* empty */
}

std::string
CMMSocketControlHdr::describe() const
{
    std::ostringstream stream;
    stream << std:: endl << "Type: " << type << " ";
    switch (type) {
    case CMM_CONTROL_MSG_BEGIN_IROB:
        stream << "IROB: " << op.begin_irob.id << " ";
        stream << "send_labels: " << op.begin_irob.send_labels << " ";
        stream << "recv_labels: " << op.begin_irob.recv_labels << " ";
        stream << "numdeps: " << op.begin_irob.numdeps;
        if (deps) {
            stream << " [ ";
            for (int i = 0; i < numdeps; i++) {
                stream << deps[i] << " ";
            }
            stream << "]";
        }
        break;
    case CMM_CONTROL_MSG_END_IROB:
        stream << "IROB: " << op.end_irob.id;
        break;
    case CMM_CONTROL_MSG_IROB_CHUNK:
        stream << "IROB: " << op.irob_chunk.id << " ";
        stream << "seqno: " << op.irob_chunk.seqno << " ";
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
        stream << "Acktype: " << op.ack.type << " ";
        stream << "IROB: " << op.ack.id;
        if (op.ack.type == ACK_TYPE_CHUNK) {
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
