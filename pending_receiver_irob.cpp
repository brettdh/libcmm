#include "pending_receiver_irob.h"

PendingReceiverIROB::PendingReceiverIROB(struct begin_irob_data begin_irob)
    : PendingIROB(begin_irob)
{
}

void
PendingReceiverIROB::add_dependent(PendingReceiverIROB *dependent)
{
    
}

void
PendingReceiverIROB::release_dependents()
{
    
}

bool 
PendingReceiverIROB::is_released(void)
{
    return deps.empty();
}
