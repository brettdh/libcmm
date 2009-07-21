#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <signal.h>
#include <unistd.h>
#include <netinet/in.h>
#include "libcmm.h"
#include "libcmm_test.h"
#include <errno.h>
#include "timeops.h"

static bool running;

void handler(int sig)
{
    running = false;
}

void str_reverse(char *str)
{
    char *head = str;
    char *tail = str + strlen(str) - 1;
    while (head < tail) {
        char tmp = *head;
        *head = *tail;
        *tail = tmp;
        head++;
        tail--;
    }
}

void * Worker(void * arg)
{
#ifdef NOMULTISOCK
    int sock = *((int*)arg);
#else    
    mc_socket_t sock = *((mc_socket_t*)arg);
#endif
    printf("Starting up on connection %d\n", sock);
    while (1) {
#ifdef NOMULTISOCK
	/* cmm_select doesn't work yet. */
	fd_set readfds;
	FD_ZERO(&readfds);
	FD_SET(sock, &readfds);
	int s_rc = select(sock+1, &readfds, NULL, NULL, NULL);
//#else
//	int s_rc = cmm_select(sock+1, &readfds, NULL, NULL, NULL);
//#endif
	if (s_rc < 0) {
	    perror("select");
	    break;
	}
#endif

        struct chunk ch;
	struct timeval begin, end, diff;
	TIME(begin);
#ifdef NOMULTISOCK
        int rc = read(sock, &ch, sizeof(ch));
#else
        u_long sender_labels = 0;
        int rc = cmm_read(sock, &ch, sizeof(ch), &sender_labels);
#endif
	TIME(end);
        if (rc != sizeof(ch)) {
	    if (rc == 0) {
		fprintf(stderr, "Connection %d closed remotely\n", sock);
	    } else {
		fprintf(stderr, "Connection %d had error %d\n", sock, errno);
		perror("cmm_read");
	    }
            break;
        }
	TIMEDIFF(begin, end, diff);
#ifdef NOMULTISOCK
	fprintf(stderr, "Received msg; took %lu.%06lu seconds\n",
		diff.tv_sec, diff.tv_usec);
#endif
        ch.data[sizeof(ch)-1] = '\0';
        printf("Msg: %*s\n", (int)(sizeof(ch) - 1), ch.data);
        //str_reverse(ch.data);
	errno = 0;
	//struct timeval begin, end, diff;
	TIME(begin);
#ifdef NOMULTISOCK
        rc = send(sock, &ch, sizeof(ch), 0);
#else
        rc = cmm_send(sock, &ch, sizeof(ch), 0, 
                      0, sender_labels, NULL, NULL);
#endif
	TIME(end);
        if (rc != sizeof(ch)) {
	    fprintf(stderr, "cmm_send returned %d (expected %u), errno=%d\n",
		    rc, sizeof(ch), errno);
            perror("cmm_send");
            break;
        }
	TIMEDIFF(begin, end, diff);
	fprintf(stderr, "Sent message; took %lu.%06lu seconds\n", 
		diff.tv_sec, diff.tv_usec);
    }
    
    printf("Done with connection %d\n", sock);
#ifdef NOMULTISOCK
    close(sock);
#else
    cmm_close(sock);
#endif
    return NULL;
}

int main()
{
    int listen_sock = socket(PF_INET, SOCK_STREAM, 0);
    if (listen_sock < 0) {
        handle_error("socket");
    }
    
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

    rc = bind(listen_sock, (struct sockaddr *)&addr, (socklen_t)sizeof(addr));
    if (rc < 0) {
        handle_error("bind");
    }
    
#ifdef NOMULTISOCK
    rc = listen(listen_sock, 5);
#else
    rc = cmm_listen(listen_sock, 5);
#endif
    if (rc < 0) {
        handle_error("cmm_listen");
    }

    running = true;
    signal(SIGINT, handler);
    signal(SIGPIPE, SIG_IGN);

    while (running) {
        struct sockaddr_in remote_addr;
        socklen_t addrlen = sizeof(remote_addr);
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(listen_sock, &readfds);
        rc = select(listen_sock + 1, &readfds, NULL, NULL, NULL);
        if (rc < 0) {
            continue;
        }

#ifdef NOMULTISOCK
        int connecting_sock = accept(listen_sock, 
				     (struct sockaddr *)&addr, 
				     &addrlen);
#else
        mc_socket_t connecting_sock = cmm_accept(listen_sock, 
                                                 (struct sockaddr *)&addr, 
                                                 &addrlen);
#endif
        if (connecting_sock < 0) {
            perror("cmm_accept");
            continue;
        }
        //pthread_t tid;
        //(void)pthread_create(&tid, NULL, Worker, (void*)&connecting_sock);
        (void)Worker((void*)&connecting_sock);
    }

    return 0;
}

