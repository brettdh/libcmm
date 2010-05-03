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
#include <signal.h>

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

    sigset_t sigset;
    sigemptyset(&sigset);
    sigaddset(&sigset, SIGPIPE); // ignore SIGPIPE
    pthread_sigmask(SIG_BLOCK, &sigset, NULL);

    {
        PthreadScopedRWLock sock_lock(&sk->my_lock, false);
        if (sk->is_non_blocking()) {
            dbgprintf("Non-blocking bootstrap. Bootstrap thread detaching.\n");
            detach();
        }
    }

    //int accept_retries = 3;
    do {
        retry = false;

        try {
            if (bootstrap_sock != -1) {
                /* we are accepting a connection */
                dbgprintf("Accepting connection; using socket %d "
                          "to bootstrap\n", bootstrap_sock);
                int num_ifaces = sk->recv_hello(bootstrap_sock);
                sk->send_hello(bootstrap_sock);

                // connecter will connect on all of its local ifaces
                //  before sending bw/latency info.
                sk->recv_remote_listeners(bootstrap_sock, num_ifaces);

                // wait for the connecter to finish, then fill in any
                //  missing connections before sending my ifaces.
                sk->startup_csocks();
                sk->wait_for_connections();
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

                    // XXX: HACK HACK HACKETY HACK to avoid failing the bootstrapper.
                    //   Right way to do this: somehow make bootstrapping reliable.
                    // if (sk->local_ifaces.size() > 1) {
                    //     int ip_rc = inet_aton("10.0.0.42", &local_addr.sin_addr);
                    //     assert(ip_rc != 0);
                    // } else {
                    memcpy(&local_addr.sin_addr, &sk->local_ifaces.begin()->ip_addr, 
                           sizeof(local_addr.sin_addr));
                    // }

                    int rc = bind(bootstrap_sock, 
                                  (struct sockaddr*)&local_addr, sizeof(local_addr));
                    if (rc < 0) {
                        status_ = errno;
                        dbgprintf("Failed to bind bootstrap socket: %s\n",
                                  strerror(errno));
                        throw rc;
                    }

                    struct sockaddr *addr = (struct sockaddr *)remote_addr;
                    sock_lock.release();
                    rc = connect(bootstrap_sock, addr, addrlen);
                    sock_lock.acquire(&sk->my_lock, false);
                    if (rc < 0) {
                        sock_lock.release();
                        sock_lock.acquire(&sk->my_lock, true);
                        status_ = errno;
                        dbgprintf("Error connecting bootstrap socket: %s\n",
                                  strerror(errno));
                        throw rc;
                    }
                }
            
                dbgprintf("Initiating connection; using socket %d "
                          "to bootstrap\n", bootstrap_sock);

                sk->send_hello(bootstrap_sock);
                int num_ifaces = sk->recv_hello(bootstrap_sock);

                // Before sending iface info, just make connections from all
                //  my local ifaces.  This way, the accept()-side will have
                //  all the connections before it needs to use any of them,
                //  and it won't try to create them.  This should avoid
                //  the race between both sides creating the same connection.
                sk->startup_csocks();
                sk->wait_for_connections();
                // After the connections are done, we still need to
                //  tell the remote side about the bw/latency of our
                //  ifaces.
                sk->send_local_listeners(bootstrap_sock);
                // during this time, the remote side will connect from
                //  all of its interfaces, if it has any besides the
                //  bootstrapping iface.
                sk->recv_remote_listeners(bootstrap_sock, num_ifaces);
            }
            
            // no errors; must have succeeded
            PthreadScopedRWLock lock(&sk->my_lock, true);
            status_ = 0;
        } catch (int error_rc) {
//             if (sk->accepting_side) {
//                 retry = ((--accept_retries) > 0);
//             }

            PthreadScopedRWLock sock_lock(&sk->my_lock, true);
            if (retry) {
                // we were interrupted by the scout bringing down this interface.
                dbgprintf("Bootstrap failed; retrying\n");
                close(bootstrap_sock);
                bootstrap_sock = -1;
                status_ = EINPROGRESS;
            } else if (status_ != EINPROGRESS) {
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
ConnBootstrapper::restart(struct net_interface down_iface,
                          bool already_locked)
{
    /* if I'm trying to bootstrap on this interface and
     * it just went away, try another interface. */

    auto_ptr<PthreadScopedRWLock> lock;
    if (!already_locked) {
        lock.reset(new PthreadScopedRWLock(&sk->my_lock, true));
    }

    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);
    if (sk->accepting_side || bootstrap_sock == -1) {
        return;
    }
    int rc = getsockname(bootstrap_sock, 
                         (struct sockaddr*)&addr, &addrlen);
    if (rc == 0 && addr.sin_addr.s_addr == down_iface.ip_addr.s_addr) {
        dbgprintf("Restarting bootstrapper (previously on %s)\n",
                  inet_ntoa(addr.sin_addr));
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
