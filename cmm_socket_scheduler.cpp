#include "cmm_socket_scheduler.h"

template <typename MsgClass>
void
CMMSocketScheduler::handle(short hdr_type, dispatch_fn_t handler)
{
    dispatcher[hdr_type] = handler;
}

template <typename MsgClass>
void
CMMSocketScheduler::dispatch(MsgClass msg)
{
    short type = ntohs(msg->type());
    if (dispatcher.find(type) == dispatcher.end()) {
        unrecognized_control_msg(msg);
    } else {
        const dispatch_fn_t& fn = dispatcher[type];
        this->*fn(msg);
    }
}

template <typename MsgClass>
void
CMMSocketScheduler::unrecognized_control_msg(MsgClass msg)
{
    throw CMMControlException("Unrecognized control message", msg);
}

template <typename MsgClass>
void
CMMSocketScheduler::enqueue(MsgClass msg)
{
    msg_queue.push(msg);
}

template <typename MsgClass>
void
CMMSocketScheduler::Run(void)
{
    while (1) {
        MsgClass msg;
        msg_queue.pop(msg);

        dispatch(msg);
    }
}

template <typename MsgClass>
CMMControlException::CMMControlException(const std::string& str, 
                                         MsgType msg_)
  : std::runtime_error(str + msg_.describe()), msg(msg_)
{
    /* empty */
}
