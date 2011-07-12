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
#include "proxy_socket.h"

typedef void * (*thread_func_t)(void *);

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

static void ServerThread(int listen_sock)
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

static bool suppress_downstream(int to_fd, char *line)
{
    return (to_fd != STDIN_FILENO);
}

const short LISTEN_PORT = 4210;

int main()
{
    int listen_sock = make_listening_socket(LISTEN_PORT + 1);
    assert(listen_sock >= 0);
    
    pthread_t server_thread, proxy_thread;
    pthread_create(&server_thread, NULL, (thread_func_t) ServerThread, (void *) listen_sock);

    start_proxy_thread(&proxy_thread, LISTEN_PORT, LISTEN_PORT + 1, print_first_lines);

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

