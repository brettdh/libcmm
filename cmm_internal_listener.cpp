#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include "cmm_internal_listener.h"
#include "debug.h"

#define INTERNAL_LISTEN_PORT 42424

ListenerThread::ListenerThread(CMMSocketImpl *sk_)
    : sk(sk_)
{
    struct sockaddr_in bind_addr;
    memset(&bind_addr, 0, sizeof(bind_addr));
    bind_addr.sin_addr.s_addr = INADDR_ANY;
    bind_addr.sin_port = htons(INTERNAL_LISTEN_PORT);
    
    listener_sock = socket(PF_INET, SOCK_STREAM, 0);
    if (listener_sock < 0) {
        throw -1;
    }

    int on = 1;
    int rc = setsockopt(listener_sock, SOL_SOCKET, SO_REUSEADDR,
                        (char *) &on, sizeof(on));
    if (rc < 0) {
        dbgprintf("Cannot reuse socket address");
    }
    
    rc = bind(listener_sock, 
              (struct sockaddr *)&bind_addr, sizeof(bind_addr));
    if (rc < 0) {
        close(listener_sock);
        throw rc;
    }
    socklen_t addrlen = sizeof(bind_addr);
    rc = getsockname(listener_sock, 
                     (struct sockaddr *)&bind_addr, &addrlen);
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
    dbgprintf("Internal socket listener listening up on port %d\n",
              ntohs(listen_port));
}

ListenerThread::~ListenerThread()
{
    close(listener_sock);
}

in_port_t
ListenerThread::port() const
{
    return listen_port;
}

void
ListenerThread::Run()
{
    while (1) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(listener_sock, &readfds);
        int rc = select(listener_sock + 1, &readfds, NULL, NULL, NULL);

        struct sockaddr_in remote_addr;
        socklen_t addrlen = sizeof(remote_addr);
        int sock = accept(listener_sock,
                          (struct sockaddr *)&remote_addr, &addrlen);
        if (sock < 0) {
	    perror("accept");
            throw std::runtime_error("Socket error");
        }

        struct sockaddr_in local_addr;
        rc = getsockname(listener_sock, 
                         (struct sockaddr *)&local_addr, &addrlen);
        if (rc < 0) {
            perror("getsockname");
            close(listener_sock);
            throw std::runtime_error("Socket error");
        }

        try {
            sk->add_connection(sock, 
                               local_addr.sin_addr, remote_addr.sin_addr);
        } catch (std::runtime_error& e) {
            dbgprintf("Failed to add connection: %s\n", e.what());
        }
    }
}
