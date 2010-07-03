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
"usage: $prog <foreground|background> <chunksize> <-- bytes\
              <send_period> <start_delay> <sending_duration> <-- seconds\
              <vanilla|intnw> <hostname>\
 Starts a single sender and single receiver thread."

void usage()
{
    fprintf(stderr, USAGE_MSG);
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
    // usage: $prog <foreground|background> <chunksize> <-- bytes
    //              <send_period> <start_delay> <sending_duration> <-- seconds
    //              <vanilla|intnw> <hostname>
    // Starts a single sender and single receiver thread.
    if (argc != 7) usage();
    bool foreground;
    if (!strcmp(argv[1], "foreground")) {
        foreground = true;
    } else if (!strcmp(argv[1], "background")) {
        foreground = false;
    } else usage();

    if (atoi(argv[2]) < 0 ||
        atoi(argv[3]) < 0 ||
        atoi(argv[4]) < 0 ||
        atoi(argv[5]) < 0) {
        fprintf(stderr, "Error: all numerical arguments must be positive integers\n");
        usage();
    }
    size_t chunksize = atoi(argv[2]);
    struct timeval send_period = { atoi(argv[3]), 0 };
    struct timeval start_delay = { atoi(argv[4]), 0 };
    struct timeval sending_duration = { atoi(argv[5]), 0 };

    bool intnw;
    if (!strcmp(argv[6], "vanilla")) {
        intnw = false;
    } else if (!strcmp(argv[1], "intnw")) {
        intnw = true;
    } else usage();

    mc_socket_t sock;
    if (intnw) {
        sock = cmm_socket(PF_INET, SOCK_STREAM, 0);
    } else {
        sock = socket(PF_INET, SOCK_STREAM, 0);
    }
    handle_error(sock < 0, intnw ? "cmm_socket" : "socket");
    
    const char *hostname = argv[7];
    int rc = cmm_connect_to(sock, hostname, MULTI_APP_TEST_PORT);
    handle_error(rc < 0, "cmm_connect");

    SenderThread sender(sock, foreground, chunksize, 
                        send_period, start_delay, sending_duration);
    ReceiverThread receiver(sock);

    ThreadGroup group;
    sender.group = &group;
    receiver.group = &group;
    group.create_thread(boost::ref(receiver));
    group.create_thread(boost::ref(sender));

    group.join_all();
    close(sock);

    // TODO: summarize stats: fg latency, bg throughput
    if (!group.fg_results.empty() && !group.bg_results.empty()) {
        fprintf(stderr, "WARNING: weirdness.  I should only have fg *or* "
                "bg results, but I have both.\n");
    }

    if (!group.fg_results.empty()) {
        fprintf(stderr, "Worker PID %d, %s%s sender results%s\n", getpid(), 
                intnw ? "intnw" : "vanilla",
                intnw ? "foreground" : "",
                foreground ? "" : " (weird)");
        for (int i = 0; i < 70; ++i) { 
            fprintf(stderr, "-");
        }
        fprintf(stderr, "\n");
        
        print_stats(group.fg_results);
    }
    if (!group.bg_results.empty()) {
        fprintf(stderr, "Worker PID %d, %s%s sender results%s\n", getpid(), 
                intnw ? "intnw" : "vanilla",
                intnw ? "background" : "",
                foreground ? " (weird)" : "");

        print_stats(group.bg_results);
    }

    return 0;
}
#endif
