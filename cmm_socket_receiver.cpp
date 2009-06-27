#include "cmm_socket_receiver.h"
#include <pthread.h>
#include "debug.h"

static void
ReceiverThread_cleanup(void * arg)
{
    CMMSocketReceiver *recvr = (CMMSocketReceiver *)arg;
    delete recvr;
}

static void *
ReceiverThread(void * arg)
{
    pthread_cleanup_push(ReceiverThread_cleanup, this);
    CMMSocketReceiver *recvr = (CMMSocketReceiver *)arg;
    recvr->RunReceiver();
    pthread_cleanup_pop(1);
    return NULL;
}

CMMSocketReceiver::CMMSocketReceiver(CMMSocketImpl *sk_)
    : sk(sk_)
{
    if (dispatcher.size() == 0) {
        dispatcher[CMM_CONTROL_MSG_BEGIN_IROB] = &CMMSocketReceiver::do_begin_irob;
        dispatcher[CMM_CONTROL_MSG_END_IROB] = &CMMSocketReceiver::do_end_irob;
        dispatcher[CMM_CONTROL_MSG_IROB_CHUNK] = &CMMSocketReceiver::do_irob_chunk;
        dispatcher[CMM_CONTROL_MSG_NEW_INTERFACE] = 
            &CMMSocketReceiver::do_new_interface;
        dispatcher[CMM_CONTROL_MSG_DOWN_INTERFACE] = 
            &CMMSocketReceiver::do_down_interface;
        dispatcher[CMM_CONTROL_MSG_ACK] = &CMMSocketReceiver::do_ack;
    }

    int rc = pthread_create(&tid, NULL, ReceiverThread, this);
    if (rc != 0) {
        throw rc;
    }
}

void
CMMSocketReceiver::enqueue(struct CMMSocketControlHdr hdr)
{
    msg_queue.push(hdr);
}

void
CMMSocketReceiver::RunReceiver(void)
{
    while (1) {
        struct CMMSocketControlHdr hdr;
        msg_queue.pop(hdr);

        bool result = dispatch(hdr);
        if (!result) {
            return;
        }
    }
}

bool 
CMMSocketReceiver::dispatch(struct CMMSocketControlHdr hdr)
{
    short type = ntohs(hdr.type);
    if (dispatcher.find(type) == dispatcher.end()) {
        return unrecognized_control_msg(hdr);
    } else {
        const dispatch_fn_t& fn = dispatcher[type];
        return this->*fn(hdr);
    }
}

bool 
CMMSocketReceiver::do_begin_irob(struct CMMSocketControlHdr hdr)
{
    
}

bool 
CMMSocketReceiver::do_end_irob(struct CMMSocketControlHdr hdr)
{
}

bool 
CMMSocketReceiver::do_irob_chunk(struct CMMSocketControlHdr hdr)
{
}

bool 
CMMSocketReceiver::do_new_interface(struct CMMSocketControlHdr hdr)
{
}

bool 
CMMSocketReceiver::do_down_interface(struct CMMSocketControlHdr hdr)
{
}

bool 
CMMSocketReceiver::do_ack(struct CMMSocketControlHdr hdr)
{
}

bool 
CMMSocketReceiver::unrecognized_control_msg(struct CMMSocketControlHdr hdr)
{
    dbgprintf("Unrecognized control message type %d\n", hdr.type);
    return false;
}


ssize_t
CMMSocketReceiver::read(void *buf, size_t len)
{

}
