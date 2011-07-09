#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <algorithm>
using std::max;

#include "test_common.h"

/* Purpose: to figure out how to proxy a TCP connection
 *          using raw sockets.
 */

const short LISTEN_PORT = 4210;

typedef void * (*thread_func)(void*);

//static pthread_mutex_t proxy_lock = PTHREAD_MUTEX_INITIALIZER;
//static pthread_cond_t proxy_cv = PTHREAD_COND_INITIALIZER;
static bool running = true;

/* from_sock and to_sock are both raw sockets. */
static int proxy_data(int from_sock, int to_sock, void (*line_proc)(char *))
{
    char buf[4096];
    int rc = read(from_sock, buf, sizeof(buf) - 1);
    if (rc > 0) {
        buf[rc] = '\0';
        if (line_proc) {
            line_proc(buf);
        }
        int bytes_written = write(to_sock, buf, rc);
        if (bytes_written != rc) {
            perror("proxy_data: write");
        }
        rc = bytes_written;
    } else if (rc == 0) {
        fprintf(stderr, "proxy_data: socket closed\n");
    } else {
        perror("proxy_data: read");
    }
    return rc;
}

// only proxy a given number of lines before simulating disconnection.
static void proxy_lines_until_closed(int client_fd, int server_fd, 
                                     void (*line_proc)(char *),
                                     int num_lines_before_break)
{
    fd_set readable;
    FD_ZERO(&readable);
    FD_SET(client_fd, &readable);
    FD_SET(server_fd, &readable);
    int nfds = max(client_fd, server_fd) + 1;

    int num_lines = 0;

    while (running) {
        int rc = select(nfds, &readable, NULL, NULL, NULL);
        if (num_lines >= num_lines_before_break) {
            // silently drop it
            continue;
        }
        if (rc > 0) {
            if (FD_ISSET(client_fd, &readable)) {
                rc = proxy_data(client_fd, server_fd, line_proc);
                num_lines++;
                if (rc > 0) {
                    FD_SET(client_fd, &readable);
                }
            }
            if (FD_ISSET(server_fd, &readable)) {
                rc = proxy_data(server_fd, client_fd, line_proc);
                num_lines++;
                if (rc > 0) {
                    FD_SET(client_fd, &readable);
                }
            }
            if (!FD_ISSET(client_fd, &readable) &&
                !FD_ISSET(server_fd, &readable)) {
                fprintf(stderr, "client and server proxy sockets closed; proxy thread exiting.\n");
                break;
            }
        } else {
            perror("select");
            break;
        }
    }
}

void ProxyThread(void *unused)
{
    int client_proxy_listen_sock = make_listening_socket(LISTEN_PORT);
    assert(client_proxy_listen_sock >= 0);

    int client_proxy_sock =  accept(client_proxy_listen_sock, NULL, NULL);
    handle_error(client_proxy_sock < 0, "accepting proxy socket");

    int server_proxy_sock = socket(PF_INET, SOCK_STREAM, 0);
    handle_error(server_proxy_sock < 0, "creating client proxy socket");

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(LISTEN_PORT + 1);
    socklen_t addrlen = sizeof(addr);
    int rc = connect(server_proxy_sock, (struct sockaddr *) &addr, addrlen);
    handle_error(rc < 0, "connecting server proxy socket");

    proxy_lines_until_closed(client_proxy_sock, server_proxy_sock, NULL, 5);

    close(server_proxy_sock);
    close(client_proxy_sock);
    close(client_proxy_listen_sock);
}


void ServerThread(int listen_sock)
{
    int sock = accept(listen_sock, NULL, NULL);
    handle_error(sock < 0, "accepting socket at server");
    
    char buf[4096];
    int rc;
    while ((rc = read(sock, buf, sizeof(buf) - 1)) > 0) {
        int bytes = rc;
        for (int i = 0; i < rc; ++i) {
            buf[i] = toupper(buf[i]);
        }
        rc = write(sock, buf, bytes);
        if (bytes != rc) {
            break;
        }
    }
    close(sock);
}

int main()
{
    int listen_sock = make_listening_socket(LISTEN_PORT + 1);
    assert(listen_sock >= 0);
    
    pthread_t server_thread, proxy_thread;
    pthread_create(&server_thread, NULL, (thread_func) ServerThread, (void *) listen_sock);
    pthread_create(&proxy_thread, NULL, (thread_func) ProxyThread, NULL);

    int client_sock = socket(PF_INET, SOCK_STREAM, 0);
    handle_error(client_sock < 0, "socket");

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(LISTEN_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    int rc = connect(client_sock, (struct sockaddr *) &addr, (socklen_t) sizeof(addr));
    handle_error(rc < 0, "connecting client socket");

    proxy_lines_until_closed(STDIN_FILENO, client_sock, NULL, -1);
    close(client_sock);

    pthread_join(proxy_thread, NULL);
    pthread_join(server_thread, NULL);

    return 0;
}
