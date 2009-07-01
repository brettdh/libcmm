#include "cmm_socket_sender.h"

CMMSocketSender::CMMSocketSender(CMMSocketImpl *sk_)
    : sk(sk_)
{
    handle(CMM_CONTROL_MSG_BEGIN_IROB, &CMMSocketSender::do_begin_irob);
    handle(CMM_CONTROL_MSG_END_IROB, &CMMSocketSender::do_end_irob);
    handle(CMM_CONTROL_MSG_IROB_CHUNK, &CMMSocketSender::do_irob_chunk);
    handle(CMM_CONTROL_MSG_NEW_INTERFACE, 
           &CMMSocketSender::do_new_interface);
    handle(CMM_CONTROL_MSG_DOWN_INTERFACE, 
           &CMMSocketSender::do_down_interface);
    handle(CMM_CONTROL_MSG_ACK, &CMMSocketSender::do_ack);
}

