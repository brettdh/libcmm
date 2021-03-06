#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <signal.h>
#include <unistd.h>
#include <netinet/in.h>
#include "libcmm.h"
#include "libcmm_test.h"
#include "libcmm_net_restriction.h"
#include "common.h"
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
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(sock, &readfds);
#ifdef NOMULTISOCK
        int s_rc = select(sock+1, &readfds, NULL, NULL, NULL);
#else
        /* XXX: make sure this is working properly. */
        int s_rc = cmm_select(sock+1, &readfds, NULL, NULL, NULL);
        //int s_rc = 1;
#endif
        if (s_rc < 0) {
            perror("select");
            break;
        }

        struct chunk ch;
        struct timeval begin, end, diff;
        TIME(begin);
#ifdef NOMULTISOCK
        int rc = read(sock, &ch, sizeof(ch));
#else
        u_long sender_labels = 0;
        int rc = cmm_read(sock, &ch, sizeof(ch), &sender_labels);

        /*
        if (sender_labels & CMM_LABEL_ONDEMAND) {
            sender_labels &= (~CMM_LABEL_WIFI_ONLY);
            sender_labels |= CMM_LABEL_THREEG_ONLY;
            printf("Got FG request: sending 3G-only response (for testing)\n");
        } else if (sender_labels & CMM_LABEL_BACKGROUND) {
            sender_labels &= (~CMM_LABEL_THREEG_ONLY);
            sender_labels |= CMM_LABEL_WIFI_ONLY;
            printf("Got BG request: sending wifi-only response (for testing)\n");
        }
        */
#endif
        TIME(end);
        if (rc != sizeof(ch)) {
            if (rc == 0) {
                printf("Connection %d closed remotely\n", sock);
            } else {
                printf("Connection %d had error %d\n", sock, errno);
                perror("cmm_read");
            }
            break;
        }
        TIMEDIFF(begin, end, diff);
        printf("[%lu.%06lu][testapp] Received msg; took %lu.%06lu seconds\n",
                end.tv_sec, end.tv_usec, diff.tv_sec, diff.tv_usec);

        ch.data[sizeof(ch)-1] = '\0';
        printf("[%lu.%06lu][testapp] Msg: %*s\n", 
                         end.tv_sec, end.tv_usec, (int)(sizeof(ch) - 1), ch.data);
        //str_reverse(ch.data);
        errno = 0;
        //struct timeval begin, end, diff;
        TIME(begin);
        printf("[%lu.%06lu][testapp] About to send response\n",
                begin.tv_sec, begin.tv_usec);
#ifdef NOMULTISOCK
        rc = send(sock, &ch, sizeof(ch), 0);
#else
        rc = cmm_send(sock, &ch, sizeof(ch), 0, 
                      sender_labels, NULL, NULL);
#endif
        TIME(end);
        if (rc != sizeof(ch)) {
            printf("cmm_send returned %d (expected %u), errno=%d\n",
                    rc, sizeof(ch), errno);
            perror("cmm_send");
            break;
        }
        TIMEDIFF(begin, end, diff);
        printf("Sent message; took %lu.%06lu seconds\n", 
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
    handle_error(listen_sock < 0, "socket");
    
    int on = 1;
    int rc = setsockopt (listen_sock, SOL_SOCKET, SO_REUSEADDR,
                         (char *) &on, sizeof(on));
    if (rc < 0) {
        printf("Cannot reuse socket address");
    }

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(LISTEN_PORT);

    rc = bind(listen_sock, (struct sockaddr *)&addr, (socklen_t)sizeof(addr));
    handle_error(rc < 0, "bind");
    
#ifdef NOMULTISOCK
    rc = listen(listen_sock, 5);
#else
    rc = cmm_listen(listen_sock, 5);
#endif
    handle_error(rc < 0, "cmm_listen");

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

