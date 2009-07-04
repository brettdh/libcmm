#include <sys/socket.h>
#include <unistd.h>
#include "cmm_internal_listener.h"

ListenerThread::ListenerThread(CMMSocketImpl *sk_)
    : sk(sk_)
{
    struct sockaddr_in bind_addr;
    memset(&bind_addr, 0, sizeof(bind_addr));
    bind_addr.sin_addr = INADDR_ANY;
    bind_addr.sin_port = 0;
    
    listener_sock = socket(PF_INET, SOCK_STREAM, 0);
    if (listener_sock < 0) {
        throw -1;
    }
    
    int rc = bind(listener_sock, 
                  (struct sockaddr *)&bind_addr,
                  sizeof(bind_addr));
    if (rc < 0) {
        close(listener_sock);
        throw rc;
    }
    rc = getsockname(listener_sock, 
                     (struct sockaddr *)&bind_addr,
                     sizeof(bind_addr));
    if (rc < 0) {
        perror("getsockname");
        close(listener_sock);
        throw rc;
    }
    listen_port = bind_addr.sin_port;
    rc = listen(listener_sock, 5);
    if (rc < 0) {
        close(listener_sock);
        throw rc;
    }
}

ListenerThread::~ListenerThread()
{
    close(listener_sock);
}

void
ListenerThread::Run()
{
    while (1) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(listener_sock, &readfds);
        int rc = select(listener_sock + 1, &readfds, NULL, NULL, NULL);

        struct sockaddr_in addr;
        socklen_t addrlen = sizeof(addr);
        int sock = accept(listener_sock, &addr, &addrlen);
        if (sock < 0) {
            throw std::runtime_error("Socket error");
        }

        sk->add_connection(sock, &addr);
    }
}
