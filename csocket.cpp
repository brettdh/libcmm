#include "cmm_socket.private.h"
#include "csocket.h"
#include "debug.h"
#include "timeops.h"
#include "cmm_socket_control.h"
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include "csocket_sender.h"
#include "csocket_receiver.h"
#include "csocket_mapping.h"

CSocketPtr
CSocket::create(CMMSocketImpl *sk_,
                struct net_interface local_iface_, 
                struct net_interface remote_iface_,
                int accepted_sock)
{
    CSocket *new_csock = new CSocket(sk_, local_iface_, 
                                     remote_iface_, accepted_sock);
    return new_csock->self_ptr;
}

CSocket::CSocket(CMMSocketImpl *sk_,
                 struct net_interface local_iface_, 
                 struct net_interface remote_iface_,
                 int accepted_sock)
    : sk(sk_),
      local_iface(local_iface_), remote_iface(remote_iface_),
      csock_sendr(NULL), csock_recvr(NULL),
      self_ptr(this)
{
    assert(sk);
    if (accepted_sock == -1) {
        osfd = socket(sk->sock_family, sk->sock_type, sk->sock_protocol);
        if (osfd < 0) {
            /* out of file descriptors or memory at this point */
            throw std::runtime_error("Out of FDs or memory!");
        }
        
        sk->set_all_sockopts(osfd);
    } else {
        osfd = accepted_sock;
	startup_workers();
    }
    
    int on = 1;
    int rc;
    /* Make sure that this socket is TCP_NODELAY for good performance */
    struct protoent *pe = getprotobyname("TCP");
    if (pe) {
	rc = setsockopt (osfd, pe->p_proto, TCP_NODELAY, 
			 (char *) &on, sizeof(on));
    } else {
	rc = setsockopt (osfd, 6, TCP_NODELAY, 
			 (char *) &on, sizeof(on));
    }
    if (rc < 0) {
	dbgprintf("Cannot make socket TCP_NODELAY");
    }
}

CSocket::~CSocket()
{
    dbgprintf("CSocket %d is being destroyed\n", osfd);
    if (osfd > 0) {
	/* if it's a real open socket */
	close(osfd);
    }    
}

int
CSocket::phys_connect()
{
    struct sockaddr_in local_addr, remote_addr;
    
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr = local_iface.ip_addr;
    local_addr.sin_port = 0;
    
    remote_addr.sin_family = AF_INET;
    remote_addr.sin_addr = remote_iface.ip_addr;
    remote_addr.sin_port = sk->remote_listener_port;
    
    int rc = bind(osfd, (struct sockaddr *)&local_addr, 
		  sizeof(local_addr));
    if (rc < 0) {
	perror("bind");
	dbgprintf("Failed to bind osfd %d to %s:%d\n",
		  osfd, inet_ntoa(local_addr.sin_addr), 
		  ntohs(local_addr.sin_port));
	return rc;
    }
    dbgprintf("Successfully bound osfd %d to %s:%d\n",
	      osfd, inet_ntoa(local_addr.sin_addr), 
	      ntohs(local_addr.sin_port));
    
    rc = connect(osfd, (struct sockaddr *)&remote_addr, 
		 sizeof(remote_addr));
    if (rc < 0) {
	perror("connect");
	dbgprintf("Failed to connect osfd %d to %s:%d\n",
		  osfd, inet_ntoa(remote_addr.sin_addr), 
		  ntohs(remote_addr.sin_port));
	return rc;
    }

    startup_workers();
    return 0;
}

void
CSocket::startup_workers()
{
    if (!csock_sendr && !csock_recvr) {
	csock_sendr = new CSocketSender(self_ptr);
	csock_recvr = new CSocketReceiver(self_ptr);
	csock_sendr->start();
	csock_recvr->start();

        self_ptr.reset();  // break the cycle
    }
}

bool 
CSocket::matches(u_long send_labels, u_long recv_labels)
{
    return sk->csock_map->csock_matches(this, send_labels, recv_labels);
}
