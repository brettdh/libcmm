#ifndef MULTI_APP_TEST_HELPER_H_INCL
#define MULTI_APP_TEST_HELPER_H_INCL

struct thread_args {
    mc_socket_t sock;
    pthread_mutex_t *mutex;
    u_long labels;
    size_t chunksize;
    struct timeval freq;
    struct timeval start_delay;
    struct timeval sending_duration;
};

void *SenderThread(struct thread_args *args);
void *ReceiverThread(struct thread_args *args);

#endif
