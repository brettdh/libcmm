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
#include <map>
using std::max; using std::map;

#include "test_common.h"
#include "proxy_socket.h"

typedef void * (*thread_func_t)(void *);

static pthread_mutex_t proxy_threads_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t proxy_threads_cv = PTHREAD_COND_INITIALIZER;

struct proxy_thread_info {
    int listen_sock;
};

static map<pthread_t, struct proxy_thread_info> proxy_threads;

static int proxy_data(int from_sock, int to_sock, chunk_proc_fn_t chunk_proc, void *proc_arg)
{
    char buf[4096];
    int rc = read(from_sock, buf, sizeof(buf) - 1);
    if (rc > 0) {
        buf[rc] = '\0';
        bool send_onward = true;
        if (chunk_proc) {
            send_onward = chunk_proc(to_sock, buf, rc, proc_arg);
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

void proxy_lines_until_closed(int client_fd, int server_fd, 
                              chunk_proc_fn_t chunk_proc, void *proc_arg)
{
    fd_set all_fds;
    FD_ZERO(&all_fds);
    FD_SET(client_fd, &all_fds);
    FD_SET(server_fd, &all_fds);
    int nfds = max(client_fd, server_fd) + 1;

    while (true) {
        fd_set readable;
        FD_ZERO(&readable);
        readable = all_fds;
        int rc = select(nfds, &readable, NULL, NULL, NULL);
        if (rc > 0) {
            if (FD_ISSET(client_fd, &readable)) {
                rc = proxy_data(client_fd, server_fd, chunk_proc, proc_arg);
                if (rc <= 0) {
                    FD_CLR(client_fd, &all_fds);
                }
            }
            if (FD_ISSET(server_fd, &readable)) {
                rc = proxy_data(server_fd, client_fd, chunk_proc, proc_arg);
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

struct proxy_args {
    in_port_t proxy_port;
    in_port_t server_port;
    int client_sock;
    chunk_proc_fn_t chunk_proc;
    void *proc_arg;
};

static void ProxyThread(struct proxy_args *args);

static void ProxyServerThread(struct proxy_args *args)
{
    in_port_t proxy_port = args->proxy_port;

    int client_proxy_listen_sock = make_listening_socket(proxy_port);
    assert(client_proxy_listen_sock >= 0);

    pthread_mutex_lock(&proxy_threads_lock);
    proxy_threads[pthread_self()].listen_sock = client_proxy_listen_sock;
    pthread_cond_signal(&proxy_threads_cv);
    pthread_mutex_unlock(&proxy_threads_lock);

    while (true) {
        int client_proxy_sock = accept(client_proxy_listen_sock, NULL, NULL);
        if (client_proxy_sock < 0) {
            break;
        }
        printf("Client connecting to proxy socket on port %hu\n", proxy_port);
        
        struct proxy_args *child_args = (struct proxy_args *) malloc(sizeof(struct proxy_args));
        memcpy(child_args, args, sizeof(struct proxy_args));
        child_args->client_sock = client_proxy_sock;
        
        pthread_t tid;
        pthread_create(&tid, NULL, (thread_func_t) ProxyThread, child_args);
        // TODO: keep track of children (unnecessary?)
    }
    // TODO: wait for children
    
    close(client_proxy_listen_sock);
    free(args);
}

static void ProxyThread(struct proxy_args *args)
{
    in_port_t server_port = args->server_port;
    chunk_proc_fn_t chunk_proc = args->chunk_proc;
    void *proc_arg = args->proc_arg;

    int client_proxy_sock = args->client_sock;

    int server_proxy_sock = socket(PF_INET, SOCK_STREAM, 0);
    handle_error(server_proxy_sock < 0, "creating client proxy socket");

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(server_port);
    socklen_t addrlen = sizeof(addr);
    int rc = connect(server_proxy_sock, (struct sockaddr *) &addr, addrlen);
    handle_error(rc < 0, "connecting server proxy socket");

    proxy_lines_until_closed(client_proxy_sock, server_proxy_sock, chunk_proc, proc_arg);

    close(server_proxy_sock);
    close(client_proxy_sock);

    free(args);
}


int start_proxy_thread(pthread_t *proxy_thread, in_port_t proxy_port, in_port_t server_port,
                       chunk_proc_fn_t chunk_proc, void *proc_arg)
{
    struct proxy_args args = {proxy_port, server_port, -1, chunk_proc, proc_arg};
    struct proxy_args *th_args = (struct proxy_args *) malloc(sizeof(struct proxy_args));
    memcpy(th_args, &args, sizeof(struct proxy_args));

    pthread_mutex_lock(&proxy_threads_lock);
    int rc = pthread_create(proxy_thread, NULL, (thread_func_t) ProxyServerThread, th_args);
    pthread_cond_wait(&proxy_threads_cv, &proxy_threads_lock);
    pthread_mutex_unlock(&proxy_threads_lock);
    return rc;
}

void stop_proxy_thread(pthread_t tid)
{
    pthread_mutex_lock(&proxy_threads_lock);
    if (proxy_threads.count(tid) > 0) {
        int listen_sock = proxy_threads[tid].listen_sock;
        close(listen_sock);
        proxy_threads.erase(tid);
    }
    pthread_mutex_unlock(&proxy_threads_lock);
}
