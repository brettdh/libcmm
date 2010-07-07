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
    
#ifdef MULTI_APP_TEST_EXECUTABLE
    // initialize from command-line args
    // Expected order: chunksize, send_period, start_delay, sending_duration
  SenderThread(char *cmdline_args[/*4*/], char *argv[]);
#endif

  private:
    void waitForResponse(int seqno);
};

struct ReceiverThread {
    mc_socket_t sock;

    AgentData *data;

    void operator()(); // thread function
    ReceiverThread(mc_socket_t sock_, AgentData *data_=NULL) 
        : sock(sock_), data(data_) {}
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
    boost::condition_variable cond;

    FILE *results_file;

AgentData() : seqno(0), results_file(NULL) {}
};

void print_stats(const TimeResultVector& results, size_t chunksize, FILE *file);

typedef enum {
    ONE_SENDER_FOREGROUND,
    ONE_SENDER_BACKGROUND,
    TWO_SENDERS
} single_app_test_mode_t;

#endif
