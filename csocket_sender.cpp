#include "csocket_sender.h"

void
CSocketSender::Run()
{
    
}

void
CSocketSender::Finish(void)
{
    csock->remove();
}

