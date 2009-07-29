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
#define SEND(fd, buf, len, flags) cmm_send(fd, buf, len, flags, 0, 0, NULL, NULL)
#define READ(fd, buf, len) cmm_read(fd, buf, len, NULL)
#define CLOSE cmm_close
#endif

#include <unistd.h>

#define LISTEN_PORT 4242

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
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "   receiver:  throughput_test -l\n");
    fprintf(stderr, "     sender:  throughput_test <host> < -b <bytes> | -k <kbytes> | -m <mbytes> >\n");
    exit(-1);
}

void calc_avg_time(struct timeval total_time, int count, struct timeval *avg_time)
{
    assert(avg_time);
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
	int rc = SEND(sock, buf+bytes_sent, bytes, 0);
	fprintf(stderr, "send returned %d\n", rc);
	if (rc < 0) {
	    handle_error("send", sock);
	}
	bytes_sent += rc;
    }
}

void send_bytes_by_chunk(int sock, char *buf, size_t bytes, size_t chunksize,
			 struct timeval *avg_time)
{
    struct timeval total_time = {0,0};
    int count = 0;
    size_t bytes_sent = 0;
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
    }

    assert(avg_time);
    calc_avg_time(total_time, count, avg_time);
}

#ifndef NOMULTISOCK
void send_bytes_by_chunk_one_irob(int sock, char *buf, size_t bytes, size_t chunksize,
				  struct timeval *avg_time)
{
    struct timeval total_time = {0, 0};
    int count = 0;
    size_t bytes_sent = 0;
    irob_id_t irob = begin_irob(sock, 0, NULL, 0, 0, NULL, NULL);
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
    }
    int rc = end_irob(irob);
    if (rc < 0) {
	fprintf(stderr, "Failed ending IROB %lu\n", irob);
	exit(-1);
    }

    assert(avg_time);
    calc_avg_time(total_time, count, avg_time);    
}
#endif

int get_int_from_string(const char *str, const char *name)
{
    errno = 0;
    int val = strtol(str, NULL, 10);
    if (errno != 0) {
	fprintf(stderr, "Error: %s argument must be an integer\n", name);
	usage();
    }
    return val;
}

int main(int argc, char *argv[])
{
    bool receiver = false;
    char ch;
    int mbytes = 0, kbytes = 0, bytes = 0;
    while ((ch = getopt(argc, argv, "lb:k:m:")) != -1) {
	switch (ch) {
	case 'l':
	    receiver = true;
	    break;
	case 'b':
	    if (bytes != 0) {
		fprintf(stderr, "Error: specify only one of -b, -k, -m\n");
		usage();
	    }
	    bytes = get_int_from_string(optarg, "bytes");
	    kbytes = bytes / 1024;
	    mbytes = kbytes / 1024;
	    break;
	case 'k':
	    if (bytes != 0) {
		fprintf(stderr, "Error: specify only one of -b, -k, -m\n");
		usage();
	    }
	    kbytes = get_int_from_string(optarg, "kbytes");
	    bytes = kbytes * 1024;
	    mbytes = kbytes / 1024;
	    if (bytes < kbytes) {
		fprintf(stderr, "Error: kbytes argument is too large! (overflow)\n");
		usage();
	    }
	    break;
	case 'm':
	    if (bytes != 0) {
		fprintf(stderr, "Error: specify only one of -b, -k, -m\n");
		usage();
	    }
	    mbytes = get_int_from_string(optarg, "mbytes");
	    kbytes = mbytes * 1024;
	    bytes = kbytes * 1024;
	    if (bytes < kbytes || kbytes < mbytes) {
		fprintf(stderr, "Error: mbytes argument is too large! (overflow)\n");
		usage();
	    }
	    break;
	}
    }
    
    if (!receiver) {
	if (bytes == 0) {
	    fprintf(stderr, "Error: sender needs positive integer "
		    "for one of -b, -k, or -m args\n");
	    usage();
	}
    }

    signal(SIGPIPE, SIG_IGN);

    if (receiver) {
	int listen_sock = socket(PF_INET, SOCK_STREAM, 0);
	if (listen_sock < 0) handle_error("socket");

	int on = 1;
	int rc = setsockopt (listen_sock, SOL_SOCKET, SO_REUSEADDR,
			     (char *) &on, sizeof(on));
	if (rc < 0) {
	    fprintf(stderr, "Cannot reuse socket address");
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

	    do {
		const int chunksize = 4096;
		char buf[chunksize];

		struct timeval begin, end, diff;
		TIME(begin);
		// XXX: should loop until recv'd all bytes,
		//      but this seems to always succeed.
		rc = READ(sock, buf, chunksize);
		TIME(end);
		TIMEDIFF(begin, end, diff);
		timeradd(&total_read_time, &diff, &total_read_time);
		total_reads++;
		if (rc < 0) {
		    handle_error("read", sock);
		} else if (rc != chunksize) {
		    break;
		}
	    } while (1);

	    CLOSE(sock);
	    printf("Closed connection %d\n", sock);

	    struct timeval avg_time = {0,0};
	    calc_avg_time(total_read_time, total_reads, &avg_time);
	    printf("Average read() time: %lu.%06lu\n",
		   avg_time.tv_sec, avg_time.tv_usec);
	} while (1);

	close(listen_sock);
    } else {
	if (!argv[optind]) {
	    fprintf(stderr, "Error: host arg required for sender\n");
	    usage();
	}

	int sock = SOCKET(PF_INET, SOCK_STREAM, 0);
	if (sock < 0) handle_error("socket");

	struct sockaddr_in srv_addr;
	socklen_t addrlen = sizeof(srv_addr);
	srv_addr.sin_family = AF_INET;
	srv_addr.sin_port = htons(LISTEN_PORT);
	const char *hostname = argv[optind];
	struct hostent *hp = gethostbyname(hostname);
	if (hp == NULL) {
	    fprintf(stderr, "Failed to lookup hostname %s\n", hostname);
	    exit(-1);
	}
	memcpy(&srv_addr.sin_addr, hp->h_addr, hp->h_length);
	
	int rc = CONNECT(sock, (struct sockaddr *)&srv_addr, addrlen);
	if (rc < 0) handle_error("connect", sock);

	struct timeval begin, end, diff;
	
	char *buf = new char[bytes];
	memset(buf, 'Q', bytes);
	if (mbytes > 0) {
	    fprintf(stderr, "Sending %dMB\n", mbytes);
	} else if (kbytes > 0) {
	    fprintf(stderr, "Sending %dKB\n", kbytes);
	} else if (bytes > 0) {
	    fprintf(stderr, "Sending %d bytes\n", bytes);
	} else assert(0);
	
	TIME(begin);
	send_bytes(sock, buf, bytes);
	TIME(end);
	TIMEDIFF(begin, end, diff);
	fprintf(stderr, "   In one chunk: %lu.%06lu seconds\n",
		diff.tv_sec, diff.tv_usec);

	for (int kchunk = 4; kchunk < kbytes; kchunk *= 2) {
	    struct timeval avg_send_time = {0,0};
	    TIME(begin);
	    send_bytes_by_chunk(sock, buf, bytes, 1024 * kchunk, &avg_send_time);
	    TIME(end);
	    TIMEDIFF(begin, end, diff);
	    fprintf(stderr, "   In %dK chunks: %lu.%06lu seconds (each send avg %lu.%06lu seconds)\n", 
		    kchunk, diff.tv_sec, diff.tv_usec,
		    avg_send_time.tv_sec, avg_send_time.tv_usec);
	}

#ifndef NOMULTISOCK
	fprintf(stderr, "  In a single IROB:\n");
	for (int kchunk = 4; kchunk < kbytes; kchunk *= 2) {
	    struct timeval avg_send_time = {0,0};
	    TIME(begin);
	    send_bytes_by_chunk_one_irob(sock, buf, bytes, 1024 * kchunk, &avg_send_time);
	    TIME(end);
	    TIMEDIFF(begin, end, diff);
	    fprintf(stderr, "   In %dK chunks: %lu.%06lu seconds (each send avg %lu.%06lu seconds)\n", 
		    kchunk, diff.tv_sec, diff.tv_usec,
		    avg_send_time.tv_sec, avg_send_time.tv_usec);
	}	
#endif

	delete [] buf;

	CLOSE(sock);
    }
    
    return 0;
}
