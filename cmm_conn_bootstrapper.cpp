#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <string.h>
#include "cmm_conn_bootstrapper.h"
#include "cmm_socket.private.h"
#include "csocket_mapping.h"
#include "debug.h"
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

ConnBootstrapper::ConnBootstrapper(CMMSocketImpl *sk_,
                                   int bootstrap_sock_,
                                   const struct sockaddr *remote_addr_,
                                   socklen_t addrlen_)
    : sk(sk_), bootstrap_sock(bootstrap_sock_), status_(EINPROGRESS),
      addrlen(addrlen_), retry(false)
{
    remote_addr = new char[addrlen];
    memcpy(remote_addr, remote_addr_, addrlen);
}

ConnBootstrapper::~ConnBootstrapper()
{
    delete [] remote_addr;
}

void
ConnBootstrapper::stop()
{
    dbgprintf("Aborting bootstrapper for multisocket %d\n",
              sk->sock);
    shutdown(bootstrap_sock, SHUT_RDWR);
}

void
ConnBootstrapper::Finish()
{
    PthreadScopedRWLock sock_lock(&sk->my_lock, true);
    close(bootstrap_sock);
    bootstrap_sock = -1;
    dbgprintf("Exiting.\n");
}


void ConnBootstrapper::Run()
{
    char name[MAX_NAME_LEN+1];
    memset(name, 0, MAX_NAME_LEN+1);
    snprintf(name, MAX_NAME_LEN, "Bootstrapper %d", sk->sock);
    set_thread_name(name);

    {
        PthreadScopedRWLock sock_lock(&sk->my_lock, false);
        if (sk->is_non_blocking()) {
            dbgprintf("Non-blocking bootstrap. Bootstrap thread detaching.\n");
            detach();
        }
    }

    do {
        try {
            if (bootstrap_sock != -1) {
                /* we are accepting a connection */
                dbgprintf("Accepting connection; using socket %d "
                          "to bootstrap\n", bootstrap_sock);
                sk->recv_remote_listeners(bootstrap_sock);
                sk->send_local_listeners(bootstrap_sock);
            } else {
                /* we are connecting */
                assert(remote_addr);
            
                {
                    PthreadScopedRWLock sock_lock(&sk->my_lock, false);
                    bootstrap_sock = socket(sk->sock_family, sk->sock_type, 
                                            sk->sock_protocol);
                    if (bootstrap_sock < 0) {
                        dbgprintf("Error creating bootstrap socket: %s\n",
                                  strerror(errno));
                        return;
                    }
            
                    if (sk->local_ifaces.empty()) {
                        dbgprintf("Error connecting bootstrap socket: no local ifaces\n");
                        return;
                    }
                    struct sockaddr_in local_addr;
                    memset(&local_addr, 0, sizeof(local_addr));
                    local_addr.sin_family = AF_INET;
                    memcpy(&local_addr.sin_addr, &sk->local_ifaces.begin()->ip_addr, 
                           sizeof(local_addr.sin_addr));
                    int rc = bind(bootstrap_sock, 
                                  (struct sockaddr*)&local_addr, sizeof(local_addr));

                    struct sockaddr *addr = (struct sockaddr *)remote_addr;
                    pthread_rwlock_unlock(&sk->my_lock);
                    rc = connect(bootstrap_sock, addr, addrlen);
                    pthread_rwlock_rdlock(&sk->my_lock);
                    if (rc < 0) {
                        status_ = errno;
                        dbgprintf("Error connecting bootstrap socket: %s\n",
                                  strerror(errno));
                        if (retry) {
                            close(bootstrap_sock);
                            bootstrap_sock = -1;

                            // we were interrupted by the scout bringing down this interface.
                            continue;
                        }
                        throw rc;
                    }
                }
            
                dbgprintf("Initiating connection; using socket %d "
                          "to bootstrap\n", bootstrap_sock);
                sk->send_local_listeners(bootstrap_sock);
                sk->recv_remote_listeners(bootstrap_sock);
            }
            
            // no errors; must have succeeded
            status_ = 0;
        } catch (int error_rc) {
            if (status_ != EINPROGRESS) {
                status_ = ECONNREFUSED;
            }
            // fall through so write-select returns
        }
    } while (retry);

    char ch = 42;
    int rc = write(sk->write_ready_pipe[1], &ch, 1);
    assert(rc == 1);
}

void
ConnBootstrapper::restart(struct net_interface down_iface)
{
    /* if I'm trying to bootstrap on this interface and
     * it just went away, try another interface. */

    PthreadScopedRWLock lock(&sk->my_lock, true);

    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);
    if (bootstrap_sock == -1) {
        return;
    }
    int rc = getsockname(bootstrap_sock, 
                         (struct sockaddr*)&addr, &addrlen);
    if (rc == 0 && addr.sin_addr.s_addr == down_iface.ip_addr.s_addr) {
        retry = true;
        shutdown(bootstrap_sock, SHUT_RDWR);
    }
}

bool 
ConnBootstrapper::succeeded()
{
    return status_ == 0;
}

bool
ConnBootstrapper::done()
{
    return status_ != EINPROGRESS;
}

int
ConnBootstrapper::status()
{
    return status_;
}
