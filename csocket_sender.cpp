#include "csocket_sender.h"

void
CSocketSender::Run()
{
    while (1) {
        pthread_mutex_lock(csock->sk->mutex);
        pthread_cond_wait();

        if (csock->sk->is_shutting_down()) {
            
        }
    }
}

void
CSocketSender::Finish(void)
{
    csock->remove();
}

