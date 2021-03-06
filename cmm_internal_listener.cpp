#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <string.h>
#include "cmm_internal_listener.h"
#include "cmm_socket.private.h"
#include "csocket_mapping.h"
#include "libcmm_net_restriction.h"
#include "debug.h"
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <signal.h>

#define INTERNAL_LISTEN_PORT 42424

ListenerThread::ListenerThread(CMMSocketImpl *sk_)
    : sk(sk_)
{
    struct sockaddr_in bind_addr;
    int rc;
    do {
        memset(&bind_addr, 0, sizeof(bind_addr));
        bind_addr.sin_addr.s_addr = INADDR_ANY;
        bind_addr.sin_port = htons(INTERNAL_LISTEN_PORT);
        
        listener_sock = socket(PF_INET, SOCK_STREAM, 0);
        if (listener_sock < 0) {
            throw -1;
        }
        
        int on = 1;
        rc = setsockopt(listener_sock, SOL_SOCKET, SO_REUSEADDR,
                            (char *) &on, sizeof(on));
        if (rc < 0) {
            dbgprintf("Cannot reuse socket address\n");
        }
        
        do {
            rc = bind(listener_sock, 
                      (struct sockaddr *)&bind_addr, sizeof(bind_addr));
            if (rc < 0) {
                if (errno == EADDRINUSE) {
                    bind_addr.sin_port = htons(ntohs(bind_addr.sin_port) + 1);
                    continue;
                } else {
                    perror("bind");
                    dbgprintf("Listener failed to bind!\n");
                    close(listener_sock);
                    throw rc;
                }
            }
        } while (rc < 0);
        
        dbgprintf("Listener bound to port %d\n", ntohs(bind_addr.sin_port));
        
        rc = listen(listener_sock, 5);
        if (rc < 0) {
            if (errno == EADDRINUSE) {
                // two intnw apps raced on the port; we lost.
                // loop back, recreate and rebind the socket
                close(listener_sock);
                dbgprintf("Listener thread lost race for port %d; rebinding\n",
                          ntohs(bind_addr.sin_port));
                continue;
            } else {
                perror("listen");
                dbgprintf("Listener thread failed to listen on socket!\n");
                close(listener_sock);
                throw rc;
            }
        }
    } while (rc < 0);

    socklen_t addrlen = sizeof(bind_addr);
    rc = getsockname(listener_sock, 
                     (struct sockaddr *)&bind_addr, &addrlen);
    if (rc < 0) {
        perror("getsockname");
        dbgprintf("Couldn't get local listener sockaddr!\n");
        close(listener_sock);
        throw rc;
    }
    if (rc < 0) {
        close(listener_sock);
        throw rc;
    }
    listen_port = bind_addr.sin_port;
    dbgprintf("Internal socket listener listening up on port %d\n",
              ntohs(listen_port));
}

void
ListenerThread::stop()
{
    dbgprintf("Shutting down listener %d (port %d)\n",
              listener_sock, ntohs(listen_port));
    shutdown(listener_sock, SHUT_RDWR);
    //stop();
    //join();
    //close(listener_sock);
}

in_port_t
ListenerThread::port() const
{
    return listen_port;
}

void
ListenerThread::Run()
{
    char name[MAX_NAME_LEN+1];
    memset(name, 0, MAX_NAME_LEN+1);
    snprintf(name, MAX_NAME_LEN, "Listener %d", listener_sock);
    set_thread_name(name);

    // we never join to this thread; we synchronize with its death
    // with a condition variable.
    detach();

    sigset_t sigset;
    sigemptyset(&sigset);
    sigaddset(&sigset, SIGPIPE); // ignore SIGPIPE
    pthread_sigmask(SIG_BLOCK, &sigset, NULL);

    while (1) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(listener_sock, &readfds);
        int rc = select(listener_sock + 1, &readfds, NULL, NULL, NULL);
        if (rc < 0) {
            if (errno == EINTR) {
                continue;
            } else {
                close(listener_sock);
                //dbgprintf("Exiting.\n");
                return;
            }
        }

        struct sockaddr_in remote_addr;
        socklen_t addrlen = sizeof(remote_addr);
        int sock = accept(listener_sock,
                          (struct sockaddr *)&remote_addr, &addrlen);
        if (sock < 0) {
            dbgprintf("Listener socket shutdown, listener thread exiting,"
                      " errno=%d\n",errno);
            close(listener_sock);
            //throw std::runtime_error("Socket error");
            //dbgprintf("Exiting.\n");
            return;
        }

        struct sockaddr_in local_addr;
        rc = getsockname(sock, 
                         (struct sockaddr *)&local_addr, &addrlen);
        if (rc < 0) {
            perror("getsockname");
            close(sock);
            dbgprintf("Error getting local socket address\n");
            continue;
        }
        
        dbgprintf("Remote host %s is connecting to local address %s\n",
                  StringifyIP(&remote_addr.sin_addr).c_str(),
                  StringifyIP(&local_addr.sin_addr).c_str());
        struct net_interface dummy;
        struct net_interface remote_iface;
//         if (sk->isLoopbackOnly()) {
//             remote_iface.ip_addr.s_addr = htonl(INADDR_LOOPBACK);
//             remote_iface.ip_addr.s_addr = htonl(INADDR_LOOPBACK);
//             remote_iface.bandwidth = 100000000;
//             remote_iface.RTT = 0;
//         } else {
        if (!sk->csock_map->get_local_iface_by_addr(local_addr.sin_addr, 
                                                    dummy)) {
            dbgprintf("%s: network should be down.  %s, go away.\n",
                      StringifyIP(&local_addr.sin_addr).c_str(),
                      StringifyIP(&remote_addr.sin_addr).c_str());
            close(sock);
            continue;
        }

        struct CMMSocketControlHdr hdr;
        rc = recv(sock, &hdr, sizeof(hdr), MSG_WAITALL);
        if (rc != sizeof(hdr)) {
            perror("recv");
            close(sock);
            dbgprintf("error receiving new_interface data\n");
            continue;
        }
            
        if (ntohs(hdr.type) != CMM_CONTROL_MSG_NEW_INTERFACE) {
            dbgprintf("Expected new-interface message on "
                      "connection start;\n   Got %s\n",
                      hdr.describe().c_str());
            close(sock);
            continue;
        }
            
        //remote_iface.ip_addr = remote_addr.sin_addr; 
        remote_iface.ip_addr = hdr.op.new_interface.ip_addr; // internal if NAT'd
        //remote_iface.external_ip_addr = remote_addr.sin_addr;
        remote_iface.bandwidth_down = ntohl(hdr.op.new_interface.bandwidth_down);
        remote_iface.bandwidth_up = ntohl(hdr.op.new_interface.bandwidth_up);
        remote_iface.RTT = ntohl(hdr.op.new_interface.RTT);
        remote_iface.type = ntohl(hdr.op.new_interface.type);
            
        struct sockaddr_in internal_remote_addr;
        memcpy(&internal_remote_addr.sin_addr, &hdr.op.new_interface.ip_addr, 
               sizeof(struct in_addr));
        dbgprintf("Adding connection %d from %s bw_down %lu bw_up %lu RTT %lu type %s(peername %s)\n",
                  sock, StringifyIP(&internal_remote_addr.sin_addr).c_str(),
                  remote_iface.bandwidth_down, remote_iface.bandwidth_up, remote_iface.RTT,
                  net_type_name(remote_iface.type),
                  StringifyIP(&remote_addr.sin_addr).c_str());
//        }
        try {
            sk->add_connection(sock, 
                               local_addr.sin_addr, 
                               remote_iface);
        } catch (std::runtime_error& e) {
            dbgprintf("Failed to add connection: %s\n", e.what());
        }
    }
}

void
ListenerThread::Finish()
{
    {
        PthreadScopedLock lock(&sk->scheduling_state_lock);
        sk->listener_thread = NULL;
        pthread_cond_signal(&sk->scheduling_state_cv);
    }

    dbgprintf("Exiting.\n");

    // nobody will pthread_join to me now, so detach
    //  to make sure the memory gets reclaimed
    //detach();

    delete this; // the last thing that will ever be done with this
}
