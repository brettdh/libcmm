#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

/* Purpose: to figure out how to proxy a TCP connection
 *          using raw sockets.
 */

const short LISTEN_PORT = 4210;

typedef void * (*thread_func)(void*);

static pthread_mutex_t proxy_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t proxy_cv = PTHREAD_COND_INITIALIZER;
static bool running = true;

/* from_sock and to_sock are both raw sockets. */
static int proxy_data(int from_sock, int to_sock)
{
    char buf[4096];
    int rc = read(from_sock, buf, sizeof(buf));
    if (rc > 0) {
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

void ProxyThread(struct sockets *sockets)
{
    int client_proxy_sock = sockets->client_proxy_sock;
    int server_proxy_sock = sockets->server_proxy_sock;
    
    fd_set readable;
    FD_ZERO(&readable);
    FD_SET(client_proxy_sock, &readable);
    FD_SET(server_proxy_sock, &readable);
    int nfds = max(proxy_sock, server_sock) + 1;

    while (running) {
        int rc = select(nfds, &readable, NULL, NULL, NULL);
        if (rc > 0) {
            if (FD_ISSET(client_proxy_sock, &readable)) {
                proxy_data(client_proxy_sock, server_proxy_sock);
                // TODO: error handling
            }
            if (FD_ISSET(server_proxy_sock, &readable)) {
                proxy_data(server_proxy_sock, client_proxy_sock);
                // TODO: error handling
            }
        }
    }
}

int main()
{
    int listen_sock = make_listening_socket(LISTEN_PORT + 1);
    assert(listen_sock >= 0);
    
    int proxy_sock = socket(PF_INET, SOCK_RAW, IPPROTO_TCP);
    handle_error(proxy_sock < 0, "making raw socket");
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(LISTEN_PORT);
    socklen_t addrlen = sizeof(addr);
    int rc = bind(proxy_sock, (struct sockaddr *) &addr, addrlen);
    handle_error(rc < 0, "binding raw socket");

    
}
