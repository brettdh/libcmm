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

typedef void * (*thread_func_t)(void*);

static pthread_mutex_t proxy_started_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t proxy_started_cv = PTHREAD_COND_INITIALIZER;
static bool running = true;

static int proxy_data(int from_sock, int to_sock, bool (*line_proc)(int, char *))
{
    char buf[4096];
    int rc = read(from_sock, buf, sizeof(buf) - 1);
    if (rc > 0) {
        buf[rc] = '\0';
        bool send_onward = true;
        if (line_proc) {
            send_onward = line_proc(to_sock, buf);
        }
        if (send_onward) {
            int bytes_written = write(to_sock, buf, rc);
            if (bytes_written != rc) {
                perror("proxy_data: write");
            }
            rc = bytes_written;
        }
    } else if (rc == 0) {
        fprintf(stderr, "proxy_data: socket closed\n");
    } else {
        perror("proxy_data: read");
    }
    return rc;
}

// only proxy a given number of lines before simulating disconnection.
//  
static void proxy_lines_until_closed(int client_fd, int server_fd, 
                                     bool (*line_proc)(int, char *))
{
    fd_set all_fds;
    FD_ZERO(&all_fds);
    FD_SET(client_fd, &all_fds);
    FD_SET(server_fd, &all_fds);
    int nfds = max(client_fd, server_fd) + 1;

    while (running) {
        fd_set readable;
        FD_ZERO(&readable);
        readable = all_fds;
        int rc = select(nfds, &readable, NULL, NULL, NULL);
        if (rc > 0) {
            if (FD_ISSET(client_fd, &readable)) {
                rc = proxy_data(client_fd, server_fd, line_proc);
                if (rc <= 0) {
                    FD_CLR(client_fd, &all_fds);
                }
            }
            if (FD_ISSET(server_fd, &readable)) {
                rc = proxy_data(server_fd, client_fd, line_proc);
                if (rc <= 0) {
                    FD_CLR(server_fd, &all_fds);
                }
            }
            if (!FD_ISSET(client_fd, &all_fds) &&
                !FD_ISSET(server_fd, &all_fds)) {
                fprintf(stderr, "client and server proxy sockets closed; proxy thread exiting.\n");
                break;
            }
        } else {
            perror("select");
            break;
        }
    }
}

static bool print_first_lines(int to_fd, char *line)
{
    static int max_lines_proxied = 10;
    static int lines_proxied = 0;

    if (lines_proxied < max_lines_proxied) {
        fprintf(stderr, "proxy'd line: %s", line);
        ++lines_proxied;
        return true;
    } else {
        fprintf(stderr, "suppressed line: %s", line);
        return false;
    }
}

struct ports {
    short proxy_port;
    short server_port;
};

void ProxyThread(struct ports *ports)
{
    short proxy_port = ports->proxy_port;
    short server_port = ports->server_port;

    int client_proxy_listen_sock = make_listening_socket(proxy_port);
    assert(client_proxy_listen_sock >= 0);

    pthread_mutex_lock(&proxy_started_lock);
    pthread_cond_signal(&proxy_started_cv);
    pthread_mutex_unlock(&proxy_started_lock);

    int client_proxy_sock =  accept(client_proxy_listen_sock, NULL, NULL);
    handle_error(client_proxy_sock < 0, "accepting proxy socket");

    int server_proxy_sock = socket(PF_INET, SOCK_STREAM, 0);
    handle_error(server_proxy_sock < 0, "creating client proxy socket");

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(server_port);
    socklen_t addrlen = sizeof(addr);
    int rc = connect(server_proxy_sock, (struct sockaddr *) &addr, addrlen);
    handle_error(rc < 0, "connecting server proxy socket");

    proxy_lines_until_closed(client_proxy_sock, server_proxy_sock, print_first_lines);

    close(server_proxy_sock);
    close(client_proxy_sock);
    close(client_proxy_listen_sock);
}


int start_proxy_thread(pthread_t *proxy_thread, short proxy_port, short server_port)
{
    struct ports ports = {proxy_port, server_port};

    pthread_mutex_lock(&proxy_started_lock);
    int rc = pthread_create(proxy_thread, NULL, (thread_func_t) ProxyThread, &ports);
    pthread_cond_wait(&proxy_started_cv, &proxy_started_lock);
    pthread_mutex_unlock(&proxy_started_lock);
    return rc;
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

bool suppress_downstream(int to_fd, char *line)
{
    return (to_fd != STDIN_FILENO);
}

int main()
{
    int listen_sock = make_listening_socket(LISTEN_PORT + 1);
    assert(listen_sock >= 0);
    
    pthread_t server_thread, proxy_thread;
    pthread_create(&server_thread, NULL, (thread_func_t) ServerThread, (void *) listen_sock);

    start_proxy_thread(&proxy_thread, LISTEN_PORT, LISTEN_PORT + 1);

    int client_sock = socket(PF_INET, SOCK_STREAM, 0);
    handle_error(client_sock < 0, "socket");

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(LISTEN_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    int rc = connect(client_sock, (struct sockaddr *) &addr, (socklen_t) sizeof(addr));
    handle_error(rc < 0, "connecting client socket");

    proxy_lines_until_closed(STDIN_FILENO, client_sock, suppress_downstream);
    close(client_sock);

    pthread_join(proxy_thread, NULL);
    pthread_join(server_thread, NULL);

    return 0;
}

