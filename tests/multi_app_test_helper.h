#ifndef MULTI_APP_TEST_HELPER_H_INCL
#define MULTI_APP_TEST_HELPER_H_INCL

#include <sys/types.h>
#include <libcmm.h>
#include <vector>
#include <map>
#include <sys/time.h>

struct packet_hdr {
    int seqno;
    size_t len;
};

#define MULTI_APP_TEST_PORT 4242

int cmm_connect_to(mc_socket_t sock, const char *hostname, uint16_t port);

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

    SenderThread(mc_socket_t sock_, bool foreground_, size_t chunksize_,
                 struct timeval send_period_, 
                 struct timeval start_delay_, 
                 struct timeval sending_duration_)
        : sock(sock_), foreground(foreground_), chunksize(chunksize_), 
          send_period(send_period_), start_delay(start_delay_), 
          sending_duration(sending_duration_) {}
};

struct ReceiverThread {
    mc_socket_t sock;

    ThreadGroup *group;

    void operator()(); // thread function
    ReceiverThread(mc_socket_t sock_) : sock(sock_) {}
};

typedef std::map<int, struct timeval> TimestampMap;
typedef std::vector<std::pair<struct timeval, struct timeval> > TimeResultVector;


#include <boost/thread.hpp>

class ThreadGroup : public boost::thread_group {
  public:
    TimestampMap fg_timestamps;
    TimestampMap bg_timestamps;
    TimeResultVector fg_results;
    TimeResultVector bg_results;
    int seqno;
    boost::mutex mutex;

    ThreadGroup() : seqno(0) {}
};

void print_stats(const TimeResultVector& results);

#endif
