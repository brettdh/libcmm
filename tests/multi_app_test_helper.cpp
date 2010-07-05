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

SenderThread::SenderThread(const char *cmdline_args[], char *prog)
    : sock(-1), foreground(true), group(NULL)
{
    chunksize = get_int_from_string(first_sender_args[0], 
                                    "chunksize", prog);
    send_period.tv_sec = get_int_from_string(first_sender_args[1], 
                                             "send_period", prog);
    send_period.tv_usec = 0;
    start_delay.tv_sec = get_int_from_string(first_sender_args[2],
                                              "start_delay", prog);
    start_delay.tv_usec = 0;
    sending_duration.tv_sec = get_int_from_string(first_sender_args[3],
                                                  "sending_duration", prog);
    sending_duration.tv_usec = 0;
}

void 
SenderThread::operator()()
{
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
            boost::lock_guard<boost::mutex> lock(group->mutex);
            hdr.seqno = htonl(group->seqno++);
        }
        
        int num_deps = (last_irob == -1) ? 0 : 1;
        irob_id_t *deps = (last_irob == -1) ? NULL : &last_irob;
        u_long labels = foreground ? CMM_LABEL_ONDEMAND : CMM_LABEL_BACKGROUND;

        TIME(now);
        {
            boost::lock_guard<boost::mutex> lock(group->mutex);
            map<int, struct timeval>& timestamps = 
                foreground ? group->fg_timestamps : group->bg_timestamps;
            timestamps[ntohl(hdr.seqno)] = now;
        }
        int rc = cmm_writev_with_deps(sock, vecs, 2, num_deps, deps,
                                      labels, NULL, NULL, &last_irob);
        if (rc != (int)(sizeof(hdr) + chunksize)) {
            fprintf(stderr, "Failed to send %s message %d\n",
                    foreground ? "foreground" : "background",
                    ntohl(hdr.seqno));
        }
        
        struct timespec dur = {send_period.tv_sec, send_period.tv_usec * 1000};
        nowake_nanosleep(&dur);

        TIME(now);
    }
    cmm_shutdown(sock, SHUT_WR);
}

void
ReceiverThread::operator()()
{
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

            struct timeval now, diff;
            TIME(now);

            boost::lock_guard<boost::mutex> lock(group->mutex);
            struct timeval begin;
            if (group->fg_timestamps.count(seqno) > 0) {
                begin = group->fg_timestamps[seqno];
                group->fg_timestamps.erase(seqno);

                TIMEDIFF(begin, now, diff);
                group->fg_results.push_back(make_pair(begin, diff));
            } else if (group->bg_timestamps.count(seqno) > 0) {
                begin = group->bg_timestamps[seqno];
                group->bg_timestamps.erase(seqno);

                TIMEDIFF(begin, now, diff);
                group->bg_results.push_back(make_pair(begin, diff));
            } else {
                fprintf(stderr, "Wha? unknown response seqno %d\n", seqno);
                break;
            }
        }
    }
}

#define USAGE_MSG \
"usage: %s <hostname> <vanilla|intnw>\n\
              <foreground|background> <chunksize> <-- bytes\n\
              <send_period> <start_delay> <sending_duration> <-- seconds\n\
 Starts a single sender and single receiver thread.
\n\
 usage: %s <hostname> <vanilla|intnw>\n\
              mix <fg_chunksize> <-- bytes\n\
              <fg_send_period> <fg_start_delay> <fg_sending_duration>\n\
              <bg_chunksize> <-- bytes\n\
              <bg_send_period> <bg_start_delay> <bg_sending_duration> <-- seconds\n\
 Starts two senders and a single receiver thread.\n\
"

void usage(char *prog)
{
    fprintf(stderr, USAGE_MSG, prog, prog);
    exit(1);
}

void print_stats(const TimeResultVector& results)
{
    for (int i = 0; i < 70; ++i) { 
        fprintf(stderr, "-");
    }
    fprintf(stderr, "\n");
    
    fprintf(stderr, "Timestamp            Response time (sec)");
    for (size_t i = 0; i < results.size(); ++i) {
        fprintf(stderr, "%lu.%06lu          %lu.%06lu\n",
                results[i].first.tv_sec, results[i].first.tv_usec,
                results[i].second.tv_sec, results[i].second.tv_usec);
    }
}

#ifdef MULTI_APP_TEST_EXECUTABLE
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

    if (argc != 7 && argc != 11) usage(argv[0]);

    bool intnw;
    if (!strcmp(vanilla_intnw_arg, "vanilla")) {
        intnw = false;
    } else if (!strcmp(vanilla_intnw_arg, "intnw")) {
        intnw = true;
    } else usage(argv[0]);

    single_app_test_mode_t mode;
    if (!strcmp(mode_arg, "foreground")) {
        mode = ONE_SENDER_FOREGROUND;
    } else if (!strcmp(mode_arg, "background")) {
        mode = ONE_SENDER_BACKGROUND;
    } else if (!strcmp(mode_arg, "mix")) {
        mode = TWO_SENDERS;
        if (argc != 11) usage(argv[0]);
    } else usage(argv[0]);

    const char **first_sender_args = &argv[4];
    const char **second_sender_args = (mode == TWO_SENDERS) ? &argv[8] : NULL;

    SenderThread first_sender(first_sender_args, argv[0]);
    SenderThread second_sender(second_sender_args, argv[0]);

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
    group.create_thread(receiver));
    group.create_thread(first_sender);
    if (mode == TWO_SENDERS) {
        second_sender.data = &data;
        second_sender.sock = sock;
        group.create_thread(second_sender);
    }
    // wait for the run to complete
    group.join_all();
    close(sock);

    // summarize stats: fg latency, bg throughput
    if (!data.fg_results.empty()) {
        fprintf(stderr, "Worker PID %d, %s foreground sender results\n", 
                getpid(), intnw ? "intnw" : "vanilla");
        
        print_stats(data.fg_results);
    }

    if (!data.bg_results.empty()) {
        fprintf(stderr, "Worker PID %d, %s background sender results\n", 
                getpid(), intnw ? "intnw" : "vanilla");

        print_stats(data.bg_results);
    }

    return 0;
}
#endif
