#include <stdio.h>
#include "libcmm.h"
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


void thread_sleep(int seconds)
{
    struct timespec timeout;
    timeout.tv_sec = seconds;
    timeout.tv_nsec = 0;
    nanosleep(&timeout, NULL);
}

#define ARG_LINE_MAX 200

struct th_arg {
    char line[ARG_LINE_MAX];

    th_arg() { line[0] = '\0'; }
    struct th_arg *clone() {
	struct th_arg *new_arg = new struct th_arg;
	memcpy(new_arg, this, sizeof(struct th_arg));
	return new_arg;
    }
};

static const char *HOST = "141.212.110.97";
static const u_short PORT = 4242;

static atomic<bool> bg_send;
static atomic<bool> running;
static atomic<mc_socket_t> shared_sock;
static struct sockaddr_in srv_addr;


void resume_bg_sends(struct th_arg* arg)
{
    fprintf(stderr, "Thunk invoked; resuming bg sends\n");
    bg_send = true;
}

void *BackgroundPing(struct th_arg * arg)
{
    char msg[1000] = "background msg ";
    char *writeptr = msg + strlen(msg);
    u_short count = 1;
    bg_send = true;
    while (running) {
	if (bg_send) {
	    snprintf(writeptr, 500, "%d", count++);

	    size_t len = strlen(msg);
	    int rc = cmm_send(shared_sock, msg, len, 0,
			      CONNMGR_LABEL_BACKGROUND,
			      (resume_handler_t)resume_bg_sends, arg);
	    if (rc == CMM_DEFERRED) {
		bg_send = false;
		fprintf(stderr, 
			"Background send %d deferred; thunk registered\n", 
			count - 1);
	    } else if (rc < 0) {
		perror("send");
		return NULL;
	    } else {
		fprintf(stderr, "sent bg message %d\n", count - 1);
	    }
	}

	thread_sleep(2);
    }
    return NULL;
}

void resume_ondemand(struct th_arg *arg)
{
    if (strlen(arg->line) > 0) {
	fprintf(stderr, "Resumed; sending thunked ondemand message\n");
	int rc = cmm_send(shared_sock, arg->line, strlen(arg->line), 0,
		      CONNMGR_LABEL_ONDEMAND, 
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
	}
    }
}

void handle_term(int)
{
    running = false;
    exit(0);
}

int srv_connect(u_long label)
{
    int rc;
    
  conn_retry:
    fprintf(stderr, "Attempting to connect to %s:%d, label %lu\n", 
	    HOST, PORT, label);
    rc = cmm_connect(shared_sock, (struct sockaddr*)&srv_addr,
		     (socklen_t)sizeof(srv_addr),
		     label);
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

int main()
{
    int rc;

    struct th_arg args;
    shared_sock = cmm_socket(PF_INET, SOCK_STREAM, 0);
    if (shared_sock < 0) {
	perror("socket");
	exit(-1);
    }

    srv_addr.sin_family = AF_INET;
    srv_addr.sin_port = htons(PORT);
    struct hostent *hp = gethostbyname(HOST);
    if (hp == NULL) {
	fprintf(stderr, "Failed to lookup hostname %s\n", HOST);
	exit(-1);
    }
    memcpy(&srv_addr.sin_addr, hp->h_addr, hp->h_length);
    
    rc = srv_connect(CONNMGR_LABEL_ONDEMAND);
    if (rc < 0) {
	if (rc == CMM_DEFERRED) {
	    fprintf(stderr, "Initial connection deferred\n");
	} else {
	    fprintf(stderr, "Initial connection failed!\n");
	    exit(-1);
	}
    }

    running = true;
    signal(SIGINT, handle_term);
    signal(SIGPIPE, SIG_IGN);

    pthread_t tid;
    rc = pthread_create(&tid, NULL, (void *(*)(void*)) BackgroundPing, &args);
    if (rc < 0) {
	fprintf(stderr, "Failed to start background thread\n");
    }

    while (running) {
	struct th_arg *new_args = args.clone();

	if (!fgets(new_args->line, ARG_LINE_MAX, stdin)) {
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
	rc = cmm_send(shared_sock, new_args->line, strlen(new_args->line), 0,
		      CONNMGR_LABEL_ONDEMAND, 
		      (resume_handler_t)resume_ondemand, new_args);
	if (rc == CMM_DEFERRED) {
	    fprintf(stderr, "Deferred\n");
	} else if (rc < 0) {
	    perror("send");
	    exit(-1);
	} else {
	    delete new_args;
	    fprintf(stderr, "...message sent.\n");
	}
    }

    pthread_join(tid, NULL);

    return 0;
}
