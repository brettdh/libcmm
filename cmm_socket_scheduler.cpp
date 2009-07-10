template <typename MsgClass>
CMMSocketScheduler<MsgClass>::~CMMSocketScheduler()
{
    while (!msg_queue.empty()) {
        MsgClass msg;
        msg_queue.pop(msg);
        msg.cleanup();
    }
}

template <typename MsgClass>
template <typename T>
void
CMMSocketScheduler<MsgClass>::handle(short hdr_type, T *obj,
                                     void (T::*fn)(MsgClass))
{
    dispatcher[hdr_type] = new HandlerImpl<T>(obj, fn);
}

template <typename MsgClass>
void
CMMSocketScheduler<MsgClass>::dispatch(MsgClass msg)
{
    short type = ntohs(msg.msgtype());
    if (dispatcher.find(type) == dispatcher.end()) {
        unrecognized_control_msg(msg);
    } else {
        Handler *handler = dispatcher[type];
        (*handler)(msg);
    }
}

template <typename MsgClass>
void
CMMSocketScheduler<MsgClass>::unrecognized_control_msg(MsgClass msg)
{
    throw CMMControlException<MsgClass>("Unrecognized control message", msg);
}

template <typename MsgClass>
void
CMMSocketScheduler<MsgClass>::enqueue(MsgClass msg)
{
    msg_queue.push(msg);
}

template <typename MsgClass>
void
CMMSocketScheduler<MsgClass>::Run(void)
{
    while (1) {
        MsgClass msg;
        msg_queue.pop(msg);

        dispatch(msg);
    }
}

template <typename MsgClass>
CMMControlException<MsgClass>::CMMControlException(const std::string& str, 
                                         MsgClass msg_)
  : std::runtime_error(str + msg_.describe()), msg(msg_)
{
    /* empty */
}
