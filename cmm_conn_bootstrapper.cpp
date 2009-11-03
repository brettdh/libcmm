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
      addrlen(addrlen_)
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
    close(bootstrap_sock);
    dbgprintf("Exiting.\n");
}


void ConnBootstrapper::Run()
{
    char name[MAX_NAME_LEN+1];
    memset(name, 0, MAX_NAME_LEN+1);
    snprintf(name, MAX_NAME_LEN, "Bootstrapper %d", sk->sock);
    set_thread_name(name);

    PthreadScopedRWLock sock_lock(&sk->my_lock, true);
      
    if (sk->is_non_blocking()) {
        dbgprintf("Non-blocking bootstrap.  Bootstrap thread detaching.\n");
        detach();
    }

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
            
            bootstrap_sock = socket(sk->sock_family, sk->sock_type, 
                                    sk->sock_protocol);
            if (bootstrap_sock < 0) {
                dbgprintf("Error creating bootstrap socket: %s\n",
                          strerror(errno));
                return;
            }
            
            struct sockaddr *addr = (struct sockaddr *)remote_addr;
            int rc = connect(bootstrap_sock, addr, addrlen);
            if (rc < 0) {
                status_ = errno;
                dbgprintf("Error connecting bootstrap socket: %s\n",
                          strerror(errno));
                throw rc;
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

    char ch = 42;
    int rc = write(sk->write_ready_pipe[1], &ch, 1);
    assert(rc == 1);
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
