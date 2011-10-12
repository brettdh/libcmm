#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include <signal.h>

#include <algorithm>
using std::min;

#include "timeops.h"

#ifdef NOMULTISOCK
#define SOCKET socket
#define CONNECT connect
#define ACCEPT accept
#define LISTEN listen
#define SEND send
#define READ read
#define CLOSE close
#else
#include "libcmm.h"
#include "libcmm_irob.h"
#define SOCKET cmm_socket
#define CONNECT cmm_connect
#define ACCEPT cmm_accept
#define LISTEN cmm_listen
#define SEND(fd, buf, len, flags) cmm_send(fd, buf, len, flags, 0, NULL, NULL)
#define READ(fd, buf, len) cmm_read(fd, buf, len, NULL)
#define CLOSE cmm_close
#endif

#include <unistd.h>

#define LISTEN_PORT 4242
static int listen_sock = -1;
static bool running = true;

void handle_term(int sig)
{
    running = false;
    if (listen_sock > 0) {
        shutdown(listen_sock, SHUT_RDWR);
    }
}

void handle_error(const char *str, int sock = -1)
{
    perror(str);
    if (sock != -1) {
        CLOSE(sock);
    }
    exit(-1);
}

void usage()
{
    dbgprintf_always("Usage:\n");
    dbgprintf_always("   receiver:  throughput_test -l\n");
    dbgprintf_always("     sender:  throughput_test <host> < -b <bytes> | -k <kbytes> | -m <mbytes> > [-c minchunksize]\n");
    dbgprintf_always("              (default minchunksize is 64 bytes; always specify in bytes)\n");
    exit(-1);
}

void calc_avg_time(struct timeval total_time, int count, struct timeval *avg_time)
{
    ASSERT(avg_time);
    avg_time->tv_sec = total_time.tv_sec / count;
    avg_time->tv_usec = total_time.tv_usec / count;

    time_t avg_seconds = avg_time->tv_sec;
    double f_seconds = ((double)total_time.tv_sec) / count;
    f_seconds -= avg_seconds;
    suseconds_t usecs = (suseconds_t)(f_seconds * 1000000);
    struct timeval extra_usecs = {0, usecs};
    timeradd(avg_time, &extra_usecs, avg_time);
}

void send_bytes(int sock, char *buf, size_t bytes)
{
    ssize_t bytes_sent = 0;
    while (bytes_sent < (ssize_t)bytes) {
#ifdef NOMULTISOCK
        int rc = SEND(sock, buf + bytes_sent, 
                      bytes - bytes_sent, 0);
#else
        int rc = cmm_send(sock, buf + bytes_sent, 
                          bytes - bytes_sent, 0,
                          CMM_LABEL_BACKGROUND, NULL, NULL);
#endif
        if (rc < 0) {
            handle_error("send", sock);
        }
        bytes_sent += rc;
    }
}

void send_bytes_by_chunk(int sock, char *buf, size_t bytes, size_t chunksize,
                         u_long stutter_ms, 
                         struct timeval *avg_time, struct timeval *avg_stutter_time)
{
    struct timeval total_time = {0,0};
    struct timeval total_stutter_time = {0,0};
    int count = 0;
    size_t bytes_sent = 0;
    struct timespec stutter = {0, stutter_ms * 1000 * 1000};
    while (bytes_sent < bytes) {
        struct timeval begin, end, diff;
        size_t len = (chunksize > (bytes - bytes_sent)) 
            ? (bytes - bytes_sent) : chunksize;

        TIME(begin);
        send_bytes(sock, buf + bytes_sent, len);
        TIME(end);
        TIMEDIFF(begin, end, diff);
        timeradd(&total_time, &diff, &total_time);
        count++;

        bytes_sent += len;

        if (stutter_ms > 0) {
            TIME(begin);
            nanosleep(&stutter, NULL);
            TIME(end);
            TIMEDIFF(begin, end, diff);
            timeradd(&total_stutter_time, &diff, &total_stutter_time);
        }
    }

    ASSERT(avg_time);
    calc_avg_time(total_time, count, avg_time);
    ASSERT(avg_stutter_time);
    calc_avg_time(total_stutter_time, count, avg_stutter_time);
}

#ifndef NOMULTISOCK
void send_bytes_by_chunk_one_irob(int sock, char *buf, size_t bytes, size_t chunksize,
                                  u_long stutter_ms, 
                                  struct timeval *avg_time, struct timeval *avg_stutter_time)
{
    struct timeval total_time = {0, 0};
    struct timeval total_stutter_time = {0, 0};
    int count = 0;
    size_t bytes_sent = 0;
    struct timespec stutter = {0, stutter_ms * 1000 * 1000};

    irob_id_t irob = begin_irob(sock, 0, NULL, CMM_LABEL_BACKGROUND, NULL, NULL);
    if (irob < 0) {
        handle_error("begin_irob", sock);
    }
    while (bytes_sent < bytes) {
        struct timeval begin, end, diff;
        size_t len = min(chunksize, (bytes - bytes_sent));

        TIME(begin);
        int rc = irob_send(irob, buf + bytes_sent, len, 0);
        if (rc < 0) {
            handle_error("irob_send", sock);
        }
        TIME(end);
        TIMEDIFF(begin, end, diff);
        timeradd(&total_time, &diff, &total_time);
        count++;

        bytes_sent += rc;

        // this doesn't really accomplish anything extra,
        //  since the lib has its own stutter.
        if (stutter_ms > 0) {
            TIME(begin);
            nanosleep(&stutter, NULL);
            TIME(end);
            TIMEDIFF(begin, end, diff);
            timeradd(&total_stutter_time, &diff, &total_stutter_time);
        }
    }
    int rc = end_irob(irob);
    if (rc < 0) {
        dbgprintf_always("Failed ending IROB %lu\n", irob);
        exit(-1);
    }

    ASSERT(avg_time);
    calc_avg_time(total_time, count, avg_time);
    ASSERT(avg_stutter_time);
    calc_avg_time(total_stutter_time, count, avg_stutter_time);
}
#endif

typedef void (*send_chunk_fn_t)(int, char *, size_t, size_t, u_long, 
                                struct timeval *, struct timeval *);

int srv_connect(const char *hostname);

ssize_t read_bytes(int sock, void *buf, size_t count)
{
    ssize_t bytes_read = 0;
    while (bytes_read < (ssize_t)count) {
        int rc = READ(sock, (char*)buf + bytes_read, 
                      count - bytes_read);
        if (rc < 0) {
            handle_error("read", sock);
        } else if (rc == 0) {
            return rc;
        }
        bytes_read += rc;
    }
    return bytes_read;
}

void run_all_chunksizes(const char *hostname, int minchunksize, 
                        char *buf, int bytes, u_long stutter_ms, 
                        send_chunk_fn_t send_chunk_fn)
{
    struct timeval begin, end, diff;
        
    for (int chunk = minchunksize; chunk <= bytes; chunk *= 2) {
        int sock = srv_connect(hostname);

        int net_bytes = htonl(bytes);
        send_bytes(sock, (char*)&net_bytes, sizeof(net_bytes));

        struct timeval avg_send_time = {0,0};
        struct timeval avg_stutter_time = {0,0};
        TIME(begin);
        send_chunk_fn(sock, buf, bytes, chunk, stutter_ms, &avg_send_time,
                      &avg_stutter_time);
        int done = 0;
        read_bytes(sock, (char*)&done, sizeof(done));
        TIME(end);
        
        CLOSE(sock);
        
        TIMEDIFF(begin, end, diff);
        dbgprintf_always("   In %d%s chunks: %lu.%06lu seconds "
                "(each send avg %lu.%06lu seconds stutter avg %lu.%06lu)\n", 
                chunk<1024?chunk:chunk/1024, chunk<1024?"B":"K",
                diff.tv_sec, diff.tv_usec,
                avg_send_time.tv_sec, avg_send_time.tv_usec,
                avg_stutter_time.tv_sec, avg_stutter_time.tv_usec);
    }
}

int get_int_from_string(const char *str, const char *name)
{
    errno = 0;
    int val = strtol(str, NULL, 10);
    if (errno != 0) {
        dbgprintf_always("Error: %s argument must be an integer\n", name);
        usage();
    }
    return val;
}

int srv_connect(const char *hostname)
{
    int sock = SOCKET(PF_INET, SOCK_STREAM, 0);
    if (sock < 0) handle_error("socket");
    
    struct sockaddr_in srv_addr;
    socklen_t addrlen = sizeof(srv_addr);
    srv_addr.sin_family = AF_INET;
    srv_addr.sin_port = htons(LISTEN_PORT);
    struct hostent *hp = gethostbyname(hostname);
    if (hp == NULL) {
        dbgprintf_always("Failed to lookup hostname %s\n", hostname);
        exit(-1);
    }
    memcpy(&srv_addr.sin_addr, hp->h_addr, hp->h_length);
    
    int rc = CONNECT(sock, (struct sockaddr *)&srv_addr, addrlen);
    if (rc < 0) handle_error("connect", sock);

    return sock;
}

int main(int argc, char *argv[])
{
    bool receiver = false;
    int ch;
    int mbytes = 0, kbytes = 0, bytes = 0;
    int minchunksize = 64;
    u_long stutter_ms = 0;
    while ((ch = getopt(argc, argv, "lb:k:m:s:c:")) != -1) {
        switch (ch) {
        case 'l':
            receiver = true;
            break;
        case 'c':
            minchunksize = get_int_from_string(optarg, "minchunksize");
            break;
        case 'b':
            if (bytes != 0) {
                dbgprintf_always("Error: specify only one of -b, -k, -m\n");
                usage();
            }
            bytes = get_int_from_string(optarg, "bytes");
            kbytes = bytes / 1024;
            mbytes = kbytes / 1024;
            break;
        case 'k':
            if (bytes != 0) {
                dbgprintf_always("Error: specify only one of -b, -k, -m\n");
                usage();
            }
            kbytes = get_int_from_string(optarg, "kbytes");
            bytes = kbytes * 1024;
            mbytes = kbytes / 1024;
            if (bytes < kbytes) {
                dbgprintf_always("Error: kbytes argument is too large! (overflow)\n");
                usage();
            }
            break;
        case 'm':
            if (bytes != 0) {
                dbgprintf_always("Error: specify only one of -b, -k, -m\n");
                usage();
            }
            mbytes = get_int_from_string(optarg, "mbytes");
            kbytes = mbytes * 1024;
            bytes = kbytes * 1024;
            if (bytes < kbytes || kbytes < mbytes) {
                dbgprintf_always("Error: mbytes argument is too large! (overflow)\n");
                usage();
            }
            break;
        case 's':
            stutter_ms = get_int_from_string(optarg, "stutter_ms");
            if (stutter_ms >= 1000) {
                dbgprintf_always("Error: stutter_ms must be <1000ms\n");
                usage();
            }
        }
    }
    
    if (!receiver) {
        if (bytes == 0) {
            dbgprintf_always("Error: sender needs positive integer "
                    "for one of -b, -k, or -m args\n");
            usage();
        }
    }

    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT, handle_term);

    if (receiver) {
        listen_sock = socket(PF_INET, SOCK_STREAM, 0);
        if (listen_sock < 0) handle_error("socket");

        int on = 1;
        int rc = setsockopt (listen_sock, SOL_SOCKET, SO_REUSEADDR,
                             (char *) &on, sizeof(on));
        if (rc < 0) {
            dbgprintf_always("Cannot reuse socket address");
        }
        
        struct sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(LISTEN_PORT);
        socklen_t addrlen = sizeof(addr);
        rc = bind(listen_sock, (struct sockaddr *)&addr, addrlen);
        if (rc < 0) handle_error("bind");

        rc = LISTEN(listen_sock, 5);
        if (rc < 0) handle_error("listen");

        do {
            int sock = ACCEPT(listen_sock, (struct sockaddr *)&addr, &addrlen);
            if (sock < 0) handle_error("accept", sock);

            printf("Accepted connection %d\n", sock);
            struct timeval total_read_time;
            int total_reads = 0;

            timerclear(&total_read_time);

            int bytes = 0;
            rc = read_bytes(sock, &bytes, sizeof(bytes));
            bytes = ntohl(bytes);
            if (bytes <= 0) {
                break;
            }
            char *buf = new char[bytes];
            
            struct timeval begin, end, diff;
            TIME(begin);
            rc = read_bytes(sock, buf, bytes);
            TIME(end);
            delete [] buf;
            TIMEDIFF(begin, end, diff);
            timeradd(&total_read_time, &diff, &total_read_time);
            total_reads++;
            if (rc == bytes) {
                int done = 0xFFFFFFFF;
                send_bytes(sock, (char*)&done, sizeof(done));
            }

            CLOSE(sock);
            printf("Closed connection %d\n", sock);

            struct timeval avg_time = {0,0};
            calc_avg_time(total_read_time, total_reads, &avg_time);
            printf("Average read() time: %lu.%06lu\n",
                   avg_time.tv_sec, avg_time.tv_usec);
        } while (running);

        close(listen_sock);
    } else {
        if (!argv[optind]) {
            dbgprintf_always("Error: host arg required for sender\n");
            usage();
        }

        char *buf = new char[bytes];
        memset(buf, 'Q', bytes);
        if (mbytes > 0) {
            dbgprintf_always("Sending %dMB\n", mbytes);
        } else if (kbytes > 0) {
            dbgprintf_always("Sending %dKB\n", kbytes);
        } else if (bytes > 0) {
            dbgprintf_always("Sending %d bytes\n", bytes);
        } else ASSERT(0);
        
        run_all_chunksizes(argv[optind], minchunksize, buf, bytes,
                           stutter_ms, send_bytes_by_chunk);

#ifndef NOMULTISOCK
        dbgprintf_always("  In a single IROB:\n");
        run_all_chunksizes(argv[optind], minchunksize, buf, bytes,
                           stutter_ms, send_bytes_by_chunk_one_irob);
#endif

        delete [] buf;
    }
    
    return 0;
}
