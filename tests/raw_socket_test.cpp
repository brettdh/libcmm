#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

const short LISTEN_PORT = 4210;

typedef void * (*thread_func)(void*);

static pthread_mutex_t proxy_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t proxy_cv = PTHREAD_COND_INITIALIZER;
static bool running = true;

void ProxyThread(struct sockets *sockets)
{
    int proxy_sock = sockets->proxy_sock;
    int server_sock = sockets->server_sock;
    
    char buf[4096];
    fd_set readable;
    FD_ZERO(&readable);
    FD_ZERO(&writeable);
    FD_SET(proxy_sock, &readable);
    FD_SET(proxy_sock, &writeable);

    while (running) {
        int rc = select(/* ... */);
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
