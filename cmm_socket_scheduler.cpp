#include "cmm_socket_scheduler.h"

static void *
SchedulerThread(void * arg)
{
    try {
        CMMSocketScheduler *scheduler = (CMMSocketScheduler *)arg;
        scheduler->Run();
    } catch(const std::exception& e) {
        dbgprintf("%s\n", e.what());
    }
    return NULL;
}

int
CMMSocketScheduler::start()
{
    return pthread_create(&tid, NULL, SchedulerThread, this);
}

CMMSocketScheduler::CMMSocketScheduler(CMMSocketImpl *sk_)
    : sk(sk_)
{
}

void
CMMSocketScheduler::handle(short hdr_type, dispatch_fn_t handler)
{
    dispatcher[hdr_type] = handler;
}

void
CMMSocketScheduler::dispatch(struct CMMSocketControlHdr hdr)
{
    short type = ntohs(hdr.type);
    if (dispatcher.find(type) == dispatcher.end()) {
        unrecognized_control_msg(hdr);
    } else {
        const dispatch_fn_t& fn = dispatcher[type];
        this->*fn(hdr);
    }
}

void
CMMSocketScheduler::unrecognized_control_msg(struct CMMSocketControlHdr hdr)
{
    throw CMMControlException("Unrecognized control message", hdr);
}

void
CMMSocketScheduler::enqueue(struct CMMSocketControlHdr hdr)
{
    msg_queue.push(hdr);
}

void
CMMSocketScheduler::Run(void)
{
    while (1) {
        struct CMMSocketControlHdr hdr;
        msg_queue.pop(hdr);

        dispatch(hdr);
    }
}
