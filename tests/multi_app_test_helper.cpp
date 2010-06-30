#include "multi_app_test_helper.h"

void *
SenderThread(struct thread_args *args)
{
    // TODO: Wait for a duration, then start sending for a duration

    return NULL;
}

void *
ReceiverThread(struct thread_args *args)
{
    // TODO: select-and-receive responses in a loop.

    return NULL;
}

#ifdef MULTI_APP_TEST_EXECUTABLE
int main(int argc, char *argv[])
{
    // TODO: create (multi)socket(s), run tests

    return 0;
}
#endif
