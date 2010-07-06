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

#define MULTI_APP_VANILLA_TEST_PORT 4242
#define MULTI_APP_INTNW_TEST_PORT 4243

int cmm_connect_to(mc_socket_t sock, const char *hostname, uint16_t port);

struct AgentData;

struct SenderThread {
    mc_socket_t sock;
    bool foreground;
    size_t chunksize;
    struct timeval send_period;
    struct timeval start_delay;
    struct timeval sending_duration;

    AgentData *data;

    void operator()(); // thread function

    SenderThread(mc_socket_t sock_, bool foreground_, size_t chunksize_,
                 int send_period_, int start_delay_, int sending_duration_);
    
    // initialize from command-line args
    // Expected order: chunksize, send_period, start_delay, sending_duration
    SenderThread(char *cmdline_args[/*4*/], char *prog);
};

struct ReceiverThread {
    mc_socket_t sock;

    AgentData *data;

    void operator()(); // thread function
    ReceiverThread(mc_socket_t sock_) : sock(sock_) {}
};

typedef std::map<int, struct timeval> TimestampMap;
typedef std::vector<std::pair<struct timeval, struct timeval> > TimeResultVector;


#include <boost/thread.hpp>

struct AgentData {
    TimestampMap fg_timestamps;
    TimestampMap bg_timestamps;
    TimeResultVector fg_results;
    TimeResultVector bg_results;
    int seqno;
    boost::mutex mutex;

    AgentData() : seqno(0) {}
};

void print_stats(const TimeResultVector& results);

typedef enum {
    ONE_SENDER_FOREGROUND,
    ONE_SENDER_BACKGROUND,
    TWO_SENDERS
} single_app_test_mode_t;

#endif
