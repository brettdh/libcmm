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
#include <functional>
using std::min; using std::max;

CSocketPtr
CSocket::create(boost::weak_ptr<CMMSocketImpl> sk_,
                struct net_interface local_iface_, 
                struct net_interface remote_iface_,
                int accepted_sock)
{
    CSocketPtr new_csock(new CSocket(sk_, local_iface_, 
                                     remote_iface_, accepted_sock));
    new_csock->self_ptr = new_csock;
    return new_csock;
}

CSocket::CSocket(boost::weak_ptr<CMMSocketImpl> sk_,
                 struct net_interface local_iface_, 
                 struct net_interface remote_iface_,
                 int accepted_sock)
    : sk(sk_),
      local_iface(local_iface_), remote_iface(remote_iface_),
      csock_sendr(NULL), csock_recvr(NULL), irob_indexes(local_iface_.labels)
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
        assert(csock_sendr == NULL && csock_recvr == NULL);
	close(osfd);
    }    
}

int
CSocket::phys_connect()
{
    struct sockaddr_in local_addr, remote_addr;
    
    // XXX-TODO: don't assume it's an inet socket

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
    struct CMMSocketControlHdr hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.type = htons(CMM_CONTROL_MSG_NEW_INTERFACE);
    hdr.send_labels = 0;
    hdr.op.new_interface.ip_addr = local_iface.ip_addr;
    hdr.op.new_interface.labels = htonl(local_iface.labels);
    rc = send(osfd, &hdr, sizeof(hdr), 0);
    if (rc != sizeof(hdr)) {
        perror("send");
        dbgprintf("Failed to send interface info\n");
        return rc;
    }

    startup_workers();
    return 0;
}

void
CSocket::startup_workers()
{
    if (!csock_sendr && !csock_recvr) {
	csock_sendr = new CSocketSender(CSocketPtr(self_ptr));
	csock_recvr = new CSocketReceiver(CSocketPtr(self_ptr));
	int rc = csock_sendr->start();
        if (rc != 0) {
            throw std::runtime_error("Failed to create csocket_sender thread!");
        }
	rc = csock_recvr->start();
        if (rc != 0) {
            throw std::runtime_error("Failed to create csocket_receiver thread!");
        }
    }
}

bool 
CSocket::matches(u_long send_labels)
{
    return sk->csock_map->csock_matches(this, send_labels);
}

u_long
CSocket::bandwidth()
{
    // TODO: replace with measurement on this socket
    u_long bw = min(local_iface.bandwidth, remote_iface.bandwidth);
    dbgprintf("bandwidth: csocket %p local %lu remote %lu\n",
              this, local_iface.bandwidth, remote_iface.bandwidth);
    dbgprintf("bandwidth of csocket %p is %lu bytes/sec\n",
              this, bw);
    return bw;
}

double CSocket::RTT()
{
    // TODO: replace with measurement on this socket
    double rtt = 2*((local_iface.RTT / 2.0) + (remote_iface.RTT / 2.0));
    dbgprintf("RTT: csocket %p local %f remote %f\n",
              this, local_iface.RTT, remote_iface.RTT);
    dbgprintf("RTT of csocket %p is %f ms\n", this, rtt);
    return rtt;
}


#define useconds(tv) ((tv).tv_sec*1000000 + (tv).tv_usec)

ssize_t
CSocket::trickle_chunksize(struct timeval time_since_last_fg,
                           struct timeval bg_wait_time)
{
    const ssize_t max_chunksize = bandwidth();
    const ssize_t min_chunksize = max(max_chunksize / 16, 64);
    ssize_t chunksize = min_chunksize * (1 << (useconds(time_since_last_fg) /
                                               useconds(sk->bg_wait_time())*2));
    if (chunksize < 0) {
        chunksize = max_chunksize;
    }
    chunksize = max(chunksize, min_chunksize);
    chunksize = min(chunksize, max_chunksize);
    return chunksize;
}
