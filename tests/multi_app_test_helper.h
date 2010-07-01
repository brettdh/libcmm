#ifndef MULTI_APP_TEST_HELPER_H_INCL
#define MULTI_APP_TEST_HELPER_H_INCL

struct packet_hdr {
    int seqno;
    size_t len;
};

class ThreadGroup;

struct SenderThread {
    mc_socket_t sock;
    pthread_mutex_t *mutex;
    bool foreground;
    size_t chunksize;
    struct timeval send_period;
    struct timeval start_delay;
    struct timeval sending_duration;

    ThreadGroup *group;

    void operator()(); // thread function
};

struct ReceiverThread {
    mc_socket_t sock;

    ThreadGroup *group;

    void operator()(); // thread function
};

class ThreadGroup : public boost::thread_group {
  public:
    std::map<int, struct timeval> fg_timestamps;
    std::map<int, struct timeval> bg_timestamps;
    std::vector<std::pair<struct timeval, struct timeval> > fg_results;
    std::vector<std::pair<struct timeval, struct timeval> > bg_results;
    int seqno;
    boost::mutex mutex;

    ThreadGroup() : seqno(0) {}
};

#endif
