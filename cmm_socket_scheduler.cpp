template <typename MsgClass>
CMMSocketScheduler<MsgClass>::CMMSocketScheduler()
{
    memset(dispatcher, 0, sizeof(Handler*) * MAX_HANDLERS);
    handle(CMM_TERMINATE_THREAD, this, 
	   &CMMSocketScheduler<MsgClass>::terminate_thread);
}

template <typename MsgClass>
CMMSocketScheduler<MsgClass>::~CMMSocketScheduler()
{
    while (!msg_queue.empty()) {
        MsgClass msg;
        msg_queue.pop(msg);
        msg.cleanup();
    }
    for (short i = 0; i < MAX_HANDLERS; i++) {
	delete dispatcher[i];
    }
}

template <typename MsgClass>
template <typename T>
void
CMMSocketScheduler<MsgClass>::handle(short hdr_type, T *obj,
                                     void (T::*fn)(MsgClass))
{
    assert(hdr_type < MAX_HANDLERS);
    assert(dispatcher[hdr_type] == NULL);
    dispatcher[hdr_type] = new HandlerImpl<T>(obj, fn);
}

template <typename MsgClass>
void
CMMSocketScheduler<MsgClass>::dispatch(MsgClass msg)
{
    short type = ntohs(msg.msgtype());
    if (type < 0 || type >= MAX_HANDLERS || dispatcher[type] == NULL) {
        unrecognized_control_msg(msg);
    } else {
        Handler *handler = dispatcher[type];
        (*handler)(msg);
    }
}

template <typename MsgClass>
void
CMMSocketScheduler<MsgClass>::stop()
{
    if (running) {
	running = false;

	MsgClass msg;
	msg.settype(htons(CMM_TERMINATE_THREAD));
	enqueue(msg);
	pthread_join(tid, NULL);
    }
}

template <typename MsgClass>
void
CMMSocketScheduler<MsgClass>::terminate_thread(MsgClass msg)
{
    throw CMMThreadFinish();
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
