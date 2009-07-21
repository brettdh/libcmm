#include <stdio.h>
#include "libcmm.h"
#include "libcmm_test.h"
#include "tbb/atomic.h"
#include "tbb/concurrent_queue.h"
using tbb::atomic;
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <connmgr_labels.h>

#include <pthread.h>
#include <signal.h>
#include <sys/time.h>
#include <time.h>
#include "timeops.h"

void thread_sleep(int seconds)
{
    struct timespec timeout;
    timeout.tv_sec = seconds;
    timeout.tv_nsec = 0;
    nanosleep(&timeout, NULL);
}

struct th_arg {
    struct chunk ch;

    th_arg() { ch.data[0] = '\0'; }
    struct th_arg *clone() {
	struct th_arg *new_arg = new struct th_arg;
	memcpy(new_arg, this, sizeof(struct th_arg));
	return new_arg;
    }
};

static const char *HOST = "141.212.110.97";

static atomic<bool> bg_send;
static atomic<bool> running;
#ifdef NOMULTISOCK
static atomic<int> shared_sock;
#else
static atomic<mc_socket_t> shared_sock;
#endif
static struct sockaddr_in srv_addr;


void resume_bg_sends(struct th_arg* arg)
{
    fprintf(stderr, "Thunk invoked; resuming bg sends\n");
    bg_send = true;
}

#ifdef NOMULTISOCK
int get_reply(int sock)
#else
int get_reply(mc_socket_t sock)
#endif
{
#if 0
    /* cmm_select doesn't work yet. */
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(sock, &readfds);
#ifdef NOMULTISOCK
    int s_rc = select(sock+1, &readfds, NULL, NULL, NULL);
#else
    int s_rc = cmm_select(sock+1, &readfds, NULL, NULL, NULL);
#endif
    if (s_rc < 0) {
	return s_rc;
    }
#endif

    struct chunk ch = {""};
    struct timeval begin, end, diff;
    TIME(begin);
#ifdef NOMULTISOCK
    int rc = read(sock, &ch, sizeof(ch));
#else
    int rc = cmm_read(sock, &ch, sizeof(ch), NULL);
#endif
    TIME(end);
    if (rc != sizeof(ch)) {
        perror("cmm_read");
        return rc;
    }
    TIMEDIFF(begin, end, diff);
    
    ch.data[sizeof(ch)-1] = '\0';
    fprintf(stderr, "Echo: %*s\n", (int)(sizeof(ch) - 1), ch.data);
    return rc;
}

void *BackgroundPing(struct th_arg * arg)
{
    struct chunk ch = {"background msg "};
    char *writeptr = ch.data + strlen(ch.data);
    u_short count = 1;
    bg_send = true;
    while (running) {
	if (bg_send) {
	    snprintf(writeptr, sizeof(ch) - strlen(ch.data) - 1, "%d", count++);

#ifdef NOMULTISOCK
	    int rc = send(shared_sock, &ch, sizeof(ch), 0);
#else
	    int rc = cmm_send(shared_sock, &ch, sizeof(ch), 0,
			      CONNMGR_LABEL_BACKGROUND, 0,
			      (resume_handler_t)resume_bg_sends, arg);
	    if (rc == CMM_DEFERRED) {
		bg_send = false;
		fprintf(stderr, 
			"Background send %d deferred; thunk registered\n", 
			count - 1);
	    } else 
#endif
	      if (rc < 0) {
		perror("send");
		return NULL;
	    } else {
		fprintf(stderr, "sent bg message %d\n", count - 1);
                rc = get_reply(shared_sock);
                if (rc < 0) {
                    return NULL;
                }
	    }
	}

	thread_sleep(2);
    }
    return NULL;
}

#ifndef NOMULTISOCK
void resume_ondemand(struct th_arg *arg)
{
    if (strlen(arg->ch.data) > 0) {
	fprintf(stderr, "Resumed; sending thunked ondemand message\n");
	int rc = cmm_send(shared_sock, arg->ch.data, sizeof(arg->ch), 0,
                          CONNMGR_LABEL_ONDEMAND, 0, 
                          (resume_handler_t)resume_ondemand, arg);
	if (rc < 0) {
	    if (rc == CMM_DEFERRED) {
		fprintf(stderr, "Deferred ondemand send re-deferred\n");
	    } else {
		perror("send");
		fprintf(stderr, "Deferred ondemand send failed!\n");
		exit(-1);
	    }
	} else {
	    fprintf(stderr, "Deferred ondemand send succeeded\n");
	    delete arg;
            rc = get_reply(shared_sock);
            if (rc < 0) {
                exit(-1);
            }
	}
    }
}
#endif

void handle_term(int)
{
    running = false;
}

int srv_connect(const char *host, u_long label)
{
    int rc;
    
  conn_retry:
    fprintf(stderr, "Attempting to connect to %s:%d, label %lu\n", 
	    host, LISTEN_PORT, label);
#ifdef NOMULTISOCK
    rc = connect(shared_sock, (struct sockaddr*)&srv_addr,
		 (socklen_t)sizeof(srv_addr));
#else
    rc = cmm_connect(shared_sock, (struct sockaddr*)&srv_addr,
		     (socklen_t)sizeof(srv_addr));
#endif
    if (rc < 0) {
	if (errno == EINTR) {
	    goto conn_retry;
	} else {
	    perror("connect");
	    fprintf(stderr, "Connection failed\n");
	    exit(-1);
	}
    } else {
	fprintf(stderr, "Connected\n");
    }
    return rc;
}

int main(int argc, char *argv[])
{
    int rc;

    srv_addr.sin_family = AF_INET;
    srv_addr.sin_port = htons(LISTEN_PORT);
    const char *hostname = (argc > 1) ? argv[1] : HOST;
    struct hostent *hp = gethostbyname(hostname);
    if (hp == NULL) {
	fprintf(stderr, "Failed to lookup hostname %s\n", HOST);
	exit(-1);
    }
    memcpy(&srv_addr.sin_addr, hp->h_addr, hp->h_length);
    
    struct th_arg args;
#ifdef NOMULTISOCK
    shared_sock = socket(PF_INET, SOCK_STREAM, 0);
#else
    shared_sock = cmm_socket(PF_INET, SOCK_STREAM, 0);
#endif
    if (shared_sock < 0) {
	perror("socket");
	exit(-1);
    }

    rc = srv_connect(hostname, CONNMGR_LABEL_ONDEMAND);
    if (rc < 0) {
#ifndef NOMULTISOCK
	if (rc == CMM_DEFERRED) {
	    fprintf(stderr, "Initial connection deferred\n");
	} else 
#endif
	  {
	    fprintf(stderr, "Initial connection failed!\n");
#ifdef NOMULTISOCK
	    close(shared_sock);
#else
	    cmm_close(shared_sock);
#endif
	    exit(-1);
	}
    }

    running = true;
    signal(SIGINT, handle_term);
    signal(SIGPIPE, SIG_IGN);

//     pthread_t tid;
//     rc = pthread_create(&tid, NULL, (void *(*)(void*)) BackgroundPing, &args);
//     if (rc < 0) {
// 	fprintf(stderr, "Failed to start background thread\n");
//     }

    while (running) {
	struct th_arg *new_args = args.clone();

	if (!fgets(new_args->ch.data, sizeof(new_args->ch) - 1, stdin)) {
	    if (errno == EINTR) {
		//fprintf(stderr, "interrupted; trying again\n");
		continue;
	    } else {
		fprintf(stderr, "fgets failed!\n");
		running = false;
		break;
	    }
	}

	fprintf(stderr, "Attempting to send message\n");
	struct timeval begin, end, diff;
	TIME(begin);
#ifdef NOMULTISOCK
	rc = send(shared_sock, new_args->ch.data, sizeof(new_args->ch), 0);
#else
	rc = cmm_send(shared_sock, new_args->ch.data, sizeof(new_args->ch), 0,
		      CONNMGR_LABEL_ONDEMAND, 0, 
		      (resume_handler_t)resume_ondemand, new_args);
	if (rc == CMM_DEFERRED) {
	    fprintf(stderr, "Deferred\n");
	} else 
#endif
	if (rc < 0) {
	    perror("send");
	    break;
	} else {
	    delete new_args;
	    TIME(end);
	    TIMEDIFF(begin, end, diff);
	    fprintf(stderr, "...message sent, took %lu.%06lu seconds\n",
		    diff.tv_sec, diff.tv_usec);
            rc = get_reply(shared_sock);
            if (rc < 0) {
                break;
            }
	}
    }

#ifdef NOMULTISOCK
    close(shared_sock);
#else
    cmm_close(shared_sock);
#endif
    //pthread_join(tid, NULL);

    return 0;
}
