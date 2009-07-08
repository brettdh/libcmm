#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <signal.h>
#include <unistd.h>
#include <netinet/in.h>
#include "libcmm.h"
#include "libcmm_test.h"

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
    mc_socket_t sock = (mc_socket_t)arg;
    printf("Starting up on connection %d\n", sock);
    while (1) {
        struct chunk ch;
        u_long sender_labels = 0;
        int rc = cmm_read(sock, &ch, sizeof(ch), &sender_labels);
        if (rc != sizeof(ch)) {
            perror("cmm_read");
            break;
        }
        ch.data[sizeof(ch)-1] = '\0';
        printf("Msg: %*s\n", sizeof(ch) - 1, ch.data);
        str_reverse(ch.data);
        rc = cmm_send(sock, &ch, sizeof(ch), 0, 
                      0, sender_labels, NULL, NULL);
        if (rc != sizeof(ch)) {
            perror("cmm_send");
            break;
        }
    }
    
    printf("Done with connection %d\n", sock);
    close(sock);
    return NULL;
}

int main()
{
    int listen_sock = socket(PF_INET, SOCK_STREAM, 0);
    if (listen_sock < 0) {
        handle_error("socket");
    }
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(LISTEN_PORT);

    int rc = bind(listen_sock, (struct sockaddr *)&addr, (socklen_t)sizeof(addr));
    if (rc < 0) {
        handle_error("bind");
    }
    
    rc = cmm_listen(listen_sock, 5);
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

        mc_socket_t connecting_sock = cmm_accept(listen_sock, 
                                                 (struct sockaddr *)&addr, 
                                                 &addrlen);
        if (connecting_sock < 0) {
            perror("cmm_accept");
            continue;
        }
        //pthread_t tid;
        //(void)pthread_create(&tid, NULL, Worker, (void*)connecting_sock);
        (void)Worker((void*)connecting_sock);
    }

    return 0;
}
