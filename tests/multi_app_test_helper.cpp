#include "multi_app_test_helper.h"
#include "test_common.h"
#include "pthread_util.h"
#include <map>
#include <vector>
using std::map;
using std::vector;
using std::pair;
using std::make_pair;

#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <libcmm_irob.h>
#include "timeops.h"

#include <boost/thread.hpp>

int cmm_connect_to(mc_socket_t sock, const char *hostname, uint16_t port)
{
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;

    if (!strcmp(hostname, "localhost")) {
        fprintf(stderr, "Using INADDR_LOOPBACK\n");
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    } else {
        fprintf(stderr, "Looking up %s\n", hostname);
        struct hostent *he = gethostbyname(hostname);
        if (!he) {
            herror("gethostbyname");
            exit(EXIT_FAILURE);
        }
        
        memcpy(&addr.sin_addr, he->h_addr, he->h_length);
        fprintf(stderr, "Resolved %s to %s\n", 
                hostname, inet_ntoa(addr.sin_addr));
    }
    addr.sin_port = htons(port);

    socklen_t addrlen = sizeof(addr);

    return cmm_connect(sock, (struct sockaddr*)&addr, addrlen);
}

SenderThread::SenderThread(mc_socket_t sock_, bool foreground_, size_t chunksize_,
                           int send_period_, int start_delay_, int sending_duration_)
    : sock(sock_), foreground(foreground_), chunksize(chunksize_)
{
    send_period.tv_sec = send_period_;
    send_period.tv_usec = 0;
    start_delay.tv_sec = start_delay_;
    start_delay.tv_usec = 0;
    sending_duration.tv_sec = sending_duration_;
    sending_duration.tv_usec = 0;
}

void 
SenderThread::operator()()
{
    assert(sock != -1 && data != NULL);

    struct packet_hdr hdr;
    hdr.seqno = 0;
    hdr.len = htonl(chunksize);
    char *buf = new char[chunksize];
    memset(buf, 42, chunksize);
    struct iovec vecs[2];
    vecs[0].iov_base = &hdr;
    vecs[0].iov_len = sizeof(hdr);
    vecs[1].iov_base = buf;
    vecs[1].iov_len = chunksize;

    irob_id_t last_irob = -1;
    
    // Wait for a duration, then start sending for a duration
    struct timespec duration = { start_delay.tv_sec, 
                                 start_delay.tv_usec * 1000 };
    nowake_nanosleep(&duration);

    struct timeval now, end_time;
    TIME(now);
    timeradd(&now, &sending_duration, &end_time);

    while (timercmp(&now, &end_time, <)) {
        {
            boost::lock_guard<boost::mutex> lock(data->mutex);
            hdr.seqno = htonl(data->seqno++);
        }
        
        int num_deps = (last_irob == -1) ? 0 : 1;
        irob_id_t *deps = (last_irob == -1) ? NULL : &last_irob;
        u_long labels = foreground ? CMM_LABEL_ONDEMAND : CMM_LABEL_BACKGROUND;

        TIME(now);
        {
            boost::lock_guard<boost::mutex> lock(data->mutex);
            map<int, struct timeval>& timestamps = 
                foreground ? data->fg_timestamps : data->bg_timestamps;
            timestamps[ntohl(hdr.seqno)] = now;
        }
        fprintf(stderr, "[PID %d] Sending %s message %d\n",
                getpid(), foreground ? "foreground" : "background",
                ntohl(hdr.seqno));
        int rc = cmm_writev_with_deps(sock, vecs, 2, num_deps, deps,
                                      labels, NULL, NULL, &last_irob);
        if (rc != (int)(sizeof(hdr) + chunksize)) {
            fprintf(stderr, "Failed to send %s message %d\n",
                    foreground ? "foreground" : "background",
                    ntohl(hdr.seqno));
        }

        waitForResponse(ntohl(hdr.seqno));

        struct timespec dur = {send_period.tv_sec, send_period.tv_usec * 1000};
        nowake_nanosleep(&dur);

        TIME(now);
    }
    cmm_shutdown(sock, SHUT_WR);
}

void SenderThread::waitForResponse(int seqno)
{
    boost::unique_lock<boost::mutex> lock(data->mutex);
    TimestampMap& send_map = (foreground
                              ? data->fg_timestamps 
                              : data->bg_timestamps);
    while (send_map.count(seqno) > 0) {
        // the receiver thread removes it and signals this CV
        data->cond.wait(lock);
    }
}

void
ReceiverThread::operator()()
{
    assert(sock != -1 && data != NULL);

    struct packet_hdr response;
    memset(&response, 0, sizeof(response));
    int rc = (int)sizeof(response);
    while (rc == (int)sizeof(response)) {
        rc = cmm_read(sock, &response, sizeof(response), NULL);
        if (rc < 0) {
            perror("cmm_read");
        } else if (rc == (int)sizeof(response)) {
            int seqno = ntohl(response.seqno);
            size_t len = ntohl(response.len);
            (void)len;

            fprintf(stderr, "[PID %d] Received response %d\n", getpid(), seqno);
            
            struct timeval now, diff;
            TIME(now);

            boost::lock_guard<boost::mutex> lock(data->mutex);
            struct timeval begin;
            if (data->fg_timestamps.count(seqno) > 0) {
                begin = data->fg_timestamps[seqno];
                data->fg_timestamps.erase(seqno);
                data->cond.notify_all();

                TIMEDIFF(begin, now, diff);
                data->fg_results.push_back(make_pair(begin, diff));
            } else if (data->bg_timestamps.count(seqno) > 0) {
                begin = data->bg_timestamps[seqno];
                data->bg_timestamps.erase(seqno);
                data->cond.notify_all();

                TIMEDIFF(begin, now, diff);
                data->bg_results.push_back(make_pair(begin, diff));
            } else {
                fprintf(stderr, "Wha? unknown response seqno %d\n", seqno);
                break;
            }
        }
    }

    boost::lock_guard<boost::mutex> lock(data->mutex);
    data->cond.notify_all();
}

void print_stats(const TimeResultVector& results, size_t chunksize, 
                 FILE *file=NULL)
{
    if (!file) {
        file = stderr;
    }
    for (int i = 0; i < 70; ++i) { 
        fprintf(file, "-");
    }
    fprintf(file, "\n");
    fprintf(file, "Chunksize: %zu\n", chunksize);
    fprintf(file, "Timestamp            Response time (sec)     Throughput (bytes/sec)\n");
    for (size_t i = 0; i < results.size(); ++i) {
        suseconds_t usecs = convert_to_useconds(results[i].second);
        double throughput = chunksize / (usecs / 1000000.0);
        fprintf(file, "%lu.%06lu          %lu.%06lu               %f\n",
                results[i].first.tv_sec, results[i].first.tv_usec,
                results[i].second.tv_sec, results[i].second.tv_usec,
                throughput);
    }
}

#ifdef MULTI_APP_TEST_EXECUTABLE
void usage(char *argv[]);

SenderThread::SenderThread(char *cmdline_args[], char *argv[])
    : sock(-1), foreground(true), data(NULL)
{
    try {
        if (cmdline_args) {
            chunksize = get_int_from_string(cmdline_args[0], 
                                            "chunksize");
            send_period.tv_sec = get_int_from_string(cmdline_args[1], 
                                                     "send_period");
            send_period.tv_usec = 0;
            start_delay.tv_sec = get_int_from_string(cmdline_args[2],
                                                     "start_delay");
            start_delay.tv_usec = 0;
            sending_duration.tv_sec = get_int_from_string(cmdline_args[3],
                                                          "sending_duration");
            sending_duration.tv_usec = 0;
        }
    } catch (std::runtime_error& e) {
        dbgprintf_always(e.what());
        usage(argv);
    }
}

#define USAGE_MSG \
"usage: %s <hostname> <vanilla|intnw>\n\
              <foreground|background> <chunksize> <-- bytes\n\
              <send_period> <start_delay> <sending_duration> <-- seconds\n\
 Starts a single sender and single receiver thread.\n\
\n\
 usage: %s <hostname> <vanilla|intnw>\n\
              mix <fg_chunksize> <-- bytes\n\
              <fg_send_period> <fg_start_delay> <fg_sending_duration>\n\
              <bg_chunksize> <-- bytes\n\
              <bg_send_period> <bg_start_delay> <bg_sending_duration> <-- seconds\n\
 Starts two senders and a single receiver thread.\n\
"

void usage(char *argv[])
{
    fprintf(stderr, "Cmdline: ");
    char **argp = &argv[0];
    while (*argp) {
        fprintf(stderr, "%s ", *argp++);
    }
    fprintf(stderr, "\n");
    fprintf(stderr, USAGE_MSG, argv[0], argv[0]);
    exit(1);
}


int main(int argc, char *argv[])
{
    // usage: $prog <hostname <vanilla|intnw>
    //              <foreground|background> <chunksize> <-- bytes
    //              <send_period> <start_delay> <sending_duration> <-- seconds
    //              
    // Starts a single sender and single receiver thread.

    // usage: $prog <hostname> <vanilla|intnw>
    //              mix <fg_chunksize> <-- bytes
    //              <fg_send_period> <fg_start_delay> <fg_sending_duration> <-- seconds
    //              <bg_chunksize> <-- bytes
    //              <bg_send_period> <bg_start_delay> <bg_sending_duration> <-- seconds
    //              
    // Starts two senders and a single receiver thread.

    const char *hostname_arg = argv[1];
    const char *vanilla_intnw_arg = argv[2];
    const char *mode_arg = argv[3];

    if (argc != 8 && argc != 12) usage(argv);

    bool intnw;
    if (!strcmp(vanilla_intnw_arg, "vanilla")) {
        intnw = false;
    } else if (!strcmp(vanilla_intnw_arg, "intnw")) {
        intnw = true;
    } else usage(argv);

    single_app_test_mode_t mode;
    if (!strcmp(mode_arg, "foreground")) {
        mode = ONE_SENDER_FOREGROUND;
    } else if (!strcmp(mode_arg, "background")) {
        mode = ONE_SENDER_BACKGROUND;
    } else if (!strcmp(mode_arg, "mix")) {
        mode = TWO_SENDERS;
        if (argc != 12) usage(argv);
    } else usage(argv);

    char **first_sender_args = &argv[4];
    char **second_sender_args = (mode == TWO_SENDERS) ? &argv[8] : NULL;

    SenderThread first_sender(first_sender_args, argv);
    SenderThread second_sender(second_sender_args, argv);

    switch (mode) {
    case ONE_SENDER_FOREGROUND:
        first_sender.foreground = true;
        break;
    case ONE_SENDER_BACKGROUND:
        first_sender.foreground = false;
        break;
    case TWO_SENDERS:
        first_sender.foreground = true;
        second_sender.foreground = false;
        break;
    }

    mc_socket_t sock;
    if (intnw) {
        sock = cmm_socket(PF_INET, SOCK_STREAM, 0);
    } else {
        sock = socket(PF_INET, SOCK_STREAM, 0);
    }
    handle_error(sock < 0, intnw ? "cmm_socket" : "socket");
    
    uint16_t port = intnw ? MULTI_APP_INTNW_TEST_PORT 
        : MULTI_APP_VANILLA_TEST_PORT;
    int rc = cmm_connect_to(sock, hostname_arg, port);
    handle_error(rc < 0, "cmm_connect");

    ReceiverThread receiver(sock);
    first_sender.sock = sock;

    AgentData data;
    boost::thread_group group;
    first_sender.data = &data;
    receiver.data = &data;
    group.create_thread(receiver);
    group.create_thread(first_sender);
    if (mode == TWO_SENDERS) {
        second_sender.data = &data;
        second_sender.sock = sock;
        group.create_thread(second_sender);
    }
    // wait for the run to complete
    group.join_all();
    close(sock);
    
    char results_filename[51];
    memset(results_filename, 0, sizeof(results_filename));
    sprintf(results_filename, "./multi_app_test_helper_%d.txt", getpid());
    FILE *output = fopen(results_filename, "w");
    if (!output) {
        perror("fopen");
        fprintf(stderr, "printing results to stderr\n");
        output = stderr;
    }

    //fprintf(output, "Results:\n");
    // summarize stats: fg latency, bg throughput
    if (!data.fg_results.empty()) {
        fprintf(output, "Worker PID %d - %s foreground sender results\n", 
                getpid(), intnw ? "intnw" : "vanilla");
        
        print_stats(data.fg_results, first_sender.chunksize, output);
    }

    if (!data.bg_results.empty()) {
        fprintf(output, "Worker PID %d - %s background sender results\n", 
                getpid(), intnw ? "intnw" : "vanilla");

        print_stats(data.bg_results, 
                    (mode == TWO_SENDERS 
                     ? &second_sender 
                     : &first_sender)->chunksize,
                    output);
    }

    if (output != stderr) {
        fclose(output);
    }

    return 0;
}
#endif
