#include "multi_app_test_helper.h"
#include "test_common.h"
#include "pthread_util.h"
#include <map>
#include <vector>
using std::map;
using std::vector;
using std::pair;
using std::make_pair;

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
    
    // TODO: Wait for a duration, then start sending for a duration
    struct timespec duration = { start_delay.tv_sec, 
                                 start_delay.tv_usec * 1000 };
    nowake_nanosleep(&duration);

    struct timeval now, end_time;
    TIME(now);
    timeradd(&now, &sending_duration, &end_time);

    while (timercmp(&now, &end_time, <)) {
        {
            scoped_lock lock(group->mutex);
            hdr.seqno = htonl(group->seqno++);
        }
        
        int num_deps = (last_irob == -1) ? 0 : 1;
        irob_id_t *deps = (last_irob == -1) ? NULL : &last_irob;
        u_long labels = foreground ? CMM_LABEL_ONDEMAND : CMM_LABEL_BACKGROUND;

        int rc = cmm_writev_with_deps(sock, vecs, 2, num_deps, deps,
                                      labels, NULL, NULL, &last_irob);
        if (rc != (sizeof(hdr) + chunksize)) {
            fprintf(stderr, "Failed to send %s message %d\n",
                    foreground ? "foreground" : "background",
                    ntohl(hdr.seqno));
        }
        // TODO: finish this function.

        TIME(now);
    }
}

void
ReceiverThread::operator()()
{
    struct packet_hdr response;
    memset(response, 0, sizeof(response));
    int rc = (int)sizeof(response);
    while (rc == (int)sizeof(response)) {
        rc = cmm_read(sock, &response, sizeof(response), NULL);
        if (rc < 0) {
            perror("cmm_read");
        } else if (rc == (int)sizeof(response)) {
            int seqno = ntohl(response.seqno);
            size_t len = ntohl(response.len);

            struct timeval now, diff;
            TIME(now);

            scoped_lock lock(group->mutex);
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

#ifdef MULTI_APP_TEST_EXECUTABLE
int main(int argc, char *argv[])
{
    // TODO: create (multi)socket(s), run tests

    return 0;
}
#endif
