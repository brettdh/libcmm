#include "csocket_sender.h"
#include "csocket.h"
#include "libcmm_irob.h"
#include "cmm_socket.private.h"

CSocketSender::CSocketSender(CSocket *csock_) : csock(csock_) {}

void
CSocketSender::Run()
{
    pthread_mutex_lock(&csock->sk->scheduling_state_lock);
    while (1) {
        if (csock->sk->is_shutting_down()) {
            if (csock->sk->outgoing_irobs.empty()) {
                goodbye();
            }
        }
        
        if (schedule_on_my_labels()) {
            continue;
        }
        if (schedule_unlabeled()) {
            continue;
        }

        pthread_cond_wait(&csock->sk->scheduling_state_cv,
                          &csock->sk->scheduling_state_lock);
        // something happened; we might be able to do some work
    }
}

template <typename ItemType, typename ContainerType>
static bool pop_item(ContainerType& container, ItemType& item)
{
    if (container.empty()) {
        return false;
    }
    
    item = *container.begin();
    container.erase(container.begin());
    return true;
}

bool
CSocketSender::schedule_on_my_labels()
{
    bool did_something = false;

    irob_id_t id;
    std::pair<irob_id_t, u_long> chunk;

    if (pop_item(csock->new_irobs, id)) {
        begin_irob(id);
        did_something = true;
    }
    
    if (pop_item(csock->new_chunks, chunk)) {
        irob_chunk(chunk.first, chunk.second);
        did_something = true;
    }
    
    if (pop_item(csock->finished_irobs, id)) {
        end_irob(id);
        did_something = true;
    }
    
    if (pop_item(csock->unacked_irobs, id)) {
        ack(id, INVALID_IROB_SEQNO);
        did_something = true;
    }
    
    if (pop_item(csock->unacked_chunks, chunk)) {
        ack(chunk.first, chunk.second);
        did_something = true;        
    }
}

bool
CSocketSender::schedule_unlabeled()
{
    irob_id_t id;
    std::pair<irob_id_t, u_long> chunk;

    if (pop_item(csock->sk->new_irobs, id)) {
        
    }
    
    if (pop_item(csock->sk->new_chunks, chunk)) {
        
    }
    
    if (pop_item(csock->sk->finished_irobs, id)) {
        
    }
    
    if (pop_item(csock->sk->unacked_irobs, id)) {
        
    }
    
    if (pop_item(csock->sk->unacked_chunks, chunk)) {
        
    }
}

void 
CSocketSender::begin_irob(irob_id_t id)
{
}

void 
CSocketSender::end_irob(irob_id_t id)
{
}

void 
CSocketSender::irob_chunk(irob_id_t id, u_long seqno)
{
}

void 
CSocketSender::new_interface(struct net_interface iface)
{
}

void 
CSocketSender::down_interface(struct net_interface iface)
{
}

void 
CSocketSender::ack(irob_id_t id, u_long seqno)
{
}

void 
CSocketSender::goodbye()
{
}


void
CSocketSender::Finish(void)
{
    csock->remove();
}

