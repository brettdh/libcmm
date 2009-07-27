#include "cmm_socket.private.h"
#include "csocket.h"
#include "debug.h"
#include "timeops.h"
#include "cmm_socket_scheduler.h"
#include "cmm_socket_control.h"
#include "cmm_socket_sender.h"
#include "cmm_socket_receiver.h"
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>

class CSocketSender : public CMMSocketScheduler<struct CMMSocketRequest> {
  public:
    explicit CSocketSender(CSocket *csock_);
  protected:    
    virtual void dispatch(struct CMMSocketRequest);
    virtual void Finish();
  private:
    CSocket *csock;
    
    void send_request(struct CMMSocketRequest req);
    void do_begin_irob(struct CMMSocketRequest req);
    void do_end_irob(struct CMMSocketRequest req);
    void do_irob_chunk(struct CMMSocketRequest req);

    void send_header(struct CMMSocketControlHdr hdr);
};

void
CSocketSender::dispatch(struct CMMSocketRequest req)
{
    CMMSocketScheduler<struct CMMSocketRequest>::dispatch(req);
}

class CSocketReceiver : public CMMSocketScheduler<struct CMMSocketControlHdr> {
  public:
    explicit CSocketReceiver(CSocket *csock_);
    void stop();
  protected:
    virtual void Run();
    virtual void Finish();
  private:
    CSocket *csock;

    void pass_header(struct CMMSocketControlHdr hdr);
    void do_begin_irob(struct CMMSocketControlHdr hdr);
    void do_irob_chunk(struct CMMSocketControlHdr hdr);
};


CSocketSender::CSocketSender(CSocket *csock_)
    : csock(csock_)
{
    handle(CMM_CONTROL_MSG_BEGIN_IROB, this, &CSocketSender::do_begin_irob);
    handle(CMM_CONTROL_MSG_END_IROB, this, &CSocketSender::send_request);
    handle(CMM_CONTROL_MSG_IROB_CHUNK, this, &CSocketSender::do_irob_chunk);
    handle(CMM_CONTROL_MSG_DEFAULT_IROB, this, &CSocketSender::do_irob_chunk);
    handle(CMM_CONTROL_MSG_NEW_INTERFACE, this, &CSocketSender::send_request);
    handle(CMM_CONTROL_MSG_DOWN_INTERFACE, this, &CSocketSender::send_request);
    handle(CMM_CONTROL_MSG_ACK, this, &CSocketSender::send_request);
    handle(CMM_CONTROL_MSG_GOODBYE, this, &CSocketSender::send_request);
}

void
CSocketSender::Finish(void)
{
    csock->remove();
}

void CSocketSender::send_header(struct CMMSocketControlHdr hdr)
{
    struct timeval now;
    TIME(now);
    dbgprintf("Sending request: %s\n",
	      hdr.describe().c_str());

    int rc = send(csock->osfd, &hdr, sizeof(hdr), 0);
    if (rc != sizeof(hdr)) {
	dbgprintf("CSocketSender: send failed, rc = %d, errno=%d\n", rc, errno);
	throw Exception::make("Socket error", hdr);
    }
}

void
CSocketSender::send_request(struct CMMSocketRequest req)
{
    struct CMMSocketControlHdr& hdr = req.hdr;
    try {
	send_header(hdr);
    } catch (std::runtime_error& e) {
        /* give this request back to the send scheduler */
        csock->sendr->enqueue(req);
        throw;
    }

    csock->sendr->signal_completion(req.requester_tid, 0);
}

void
CSocketSender::do_begin_irob(struct CMMSocketRequest req)
{
    struct CMMSocketControlHdr& hdr = req.hdr;
    assert(ntohs(hdr.type) == CMM_CONTROL_MSG_BEGIN_IROB);
    irob_id_t *deps = hdr.op.begin_irob.deps;
    hdr.op.begin_irob.deps = NULL; 

    try {
	int numdeps = ntohl(hdr.op.begin_irob.numdeps);
	if (numdeps > 0) {
	    assert(deps);
	    int datalen = numdeps * sizeof(irob_id_t);
	    send_header(hdr);
	    int rc = send(csock->osfd, deps, datalen, 0);
	    if (rc != datalen) {
		dbgprintf("CSocketSender: send failed, rc = %d, errno=%d\n", rc, errno);
		throw Exception::make("Socket error", req);
	    }
	    /* this buffer is no longer needed, so delete it */
	    delete [] deps;
	} else {
	    send_header(hdr);
	}
    } catch (std::runtime_error& e) {
	hdr.op.begin_irob.deps = deps;
	csock->sendr->enqueue(req);
	throw;
    }
    csock->sendr->signal_completion(req.requester_tid, 0);
}

void
CSocketSender::do_irob_chunk(struct CMMSocketRequest req)
{
    struct CMMSocketControlHdr& hdr = req.hdr;
    int datalen = 0;
    short type = ntohs(hdr.type);
    if (type == CMM_CONTROL_MSG_IROB_CHUNK) {
	datalen = ntohl(hdr.op.irob_chunk.datalen);
    } else if (type == CMM_CONTROL_MSG_DEFAULT_IROB) {
	datalen = ntohl(hdr.op.default_irob.datalen);
    } else assert(0);
    char *& data_r = ((type == CMM_CONTROL_MSG_IROB_CHUNK) 
		      ? hdr.op.irob_chunk.data
		      : hdr.op.default_irob.data);
    if (datalen <= 0) {
        throw Exception::make("Expected data with header, got none", req);
    }
    
    char *buf = data_r;
    data_r = NULL;
    
    try {
	send_header(hdr);
	int rc = send(csock->osfd, buf, datalen, 0);
	if (rc != datalen) {
	    dbgprintf("CSocketSender: send failed, rc = %d, errno=%d\n", rc, errno);
	    throw Exception::make("Socket error", req);
	}
    } catch (std::runtime_error& e) {
	data_r = buf;
	csock->sendr->enqueue(req);
	throw;
    }
    /* notice that we don't delete the data buffer here, 
     * since it is an un-ACK'd part of an IROB. 
     * It will be cleaned up later when the ACK is received. */

    csock->sendr->signal_completion(req.requester_tid, datalen);
}

CSocketReceiver::CSocketReceiver(CSocket *csock_)
    : csock(csock_)
{
    handle(CMM_CONTROL_MSG_BEGIN_IROB, this, &CSocketReceiver::do_begin_irob);
    handle(CMM_CONTROL_MSG_END_IROB, this, &CSocketReceiver::pass_header);
    handle(CMM_CONTROL_MSG_IROB_CHUNK, this, &CSocketReceiver::do_irob_chunk);
    handle(CMM_CONTROL_MSG_DEFAULT_IROB, this, &CSocketReceiver::do_irob_chunk);
    handle(CMM_CONTROL_MSG_NEW_INTERFACE, this, &CSocketReceiver::pass_header);
    handle(CMM_CONTROL_MSG_DOWN_INTERFACE, this, 
           &CSocketReceiver::pass_header);
    handle(CMM_CONTROL_MSG_ACK, this, &CSocketReceiver::pass_header);
    handle(CMM_CONTROL_MSG_GOODBYE, this, &CSocketReceiver::pass_header);
}

void
CSocketReceiver::Run(void)
{
    while (1) {
        struct CMMSocketControlHdr hdr;

	dbgprintf("About to wait for network messages\n");
#if 0
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(csock->osfd, &readfds);
        int rc = select(csock->osfd + 1, &readfds, NULL, NULL, NULL);
        if (rc < 0) {
            if (errno == EINTR) {
                continue;
            } else {
		dbgprintf("CSocketReceiver: select failed, errno=%d\n", errno);
                return;
            }
        }

	dbgprintf("Network message incoming\n");
#endif

	struct timeval begin, end, diff;
	TIME(begin);

        int rc = recv(csock->osfd, &hdr, sizeof(hdr), 0);
        if (rc != sizeof(hdr)) {
	    dbgprintf("CSocketReceiver: recv failed, rc = %d, errno=%d\n", rc, errno);
            return;
        }

	dbgprintf("Received message: %s\n", hdr.describe().c_str());

        dispatch(hdr);

	TIME(end);
	TIMEDIFF(begin, end, diff);
	dbgprintf("Worker-receiver passed message in %lu.%06lu seconds (%s)\n",
		  diff.tv_sec, diff.tv_usec, hdr.describe().c_str());
    }
}

void
CSocketReceiver::stop(void)
{
    CMMThread::stop();
}

void
CSocketReceiver::Finish(void)
{
    /* Whether CSocketSender or CSocketReceiver gets here 
     * first, the result should be correct.
     * Everything should only get deleted once, and the
     * thread-stopping functions are idempotent. */
    csock->remove();
}

void CSocketReceiver::pass_header(struct CMMSocketControlHdr hdr)
{
    csock->recvr->enqueue(hdr);
}

void CSocketReceiver::do_begin_irob(struct CMMSocketControlHdr hdr)
{
    assert(ntohs(hdr.type) == CMM_CONTROL_MSG_BEGIN_IROB);
    int numdeps = ntohl(hdr.op.begin_irob.numdeps);
    if (numdeps > 0) {
        irob_id_t *deps = new irob_id_t[numdeps];
        int datalen = numdeps * sizeof(irob_id_t);
        int rc = recv(csock->osfd, (char*)deps, datalen, 0);
        if (rc != datalen) {
            if (rc < 0) {
                dbgprintf("Error %d on socket %d\n", errno, csock->osfd);
            } else {
                dbgprintf("Expected %d bytes after header, received %d\n", 
                          datalen, rc);
            }
            delete [] deps;
            throw Exception::make("Socket error", hdr);
        }
        hdr.op.begin_irob.deps = deps;
    }

    pass_header(hdr);    
}

void CSocketReceiver::do_irob_chunk(struct CMMSocketControlHdr hdr)
{
    assert(ntohs(hdr.type) == CMM_CONTROL_MSG_IROB_CHUNK || 
	   ntohs(hdr.type) == CMM_CONTROL_MSG_DEFAULT_IROB);
    short type = ntohs(hdr.type);
    int datalen = ntohl((type == CMM_CONTROL_MSG_IROB_CHUNK) 
			? hdr.op.irob_chunk.datalen
			: hdr.op.default_irob.datalen);
    if (datalen <= 0) {
        throw Exception::make("Expected data with header, got none", hdr);
    }
    
    char *buf = new char[datalen];
    int rc = recv(csock->osfd, buf, datalen, MSG_WAITALL);
    if (rc != datalen) {
        if (rc < 0) {
            dbgprintf("Error %d on socket %d\n", errno, csock->osfd);
        } else {
            dbgprintf("Expected %d bytes after header, received %d\n", 
                      datalen, rc);
        }
        delete [] buf;
        throw Exception::make("Socket error", hdr);
    }
    dbgprintf("Successfully got %d data bytes\n", datalen);

    if (type == CMM_CONTROL_MSG_IROB_CHUNK) {
	hdr.op.irob_chunk.data = buf;
    } else {
	hdr.op.default_irob.data = buf;
    }
    pass_header(hdr);
}

CSocket::CSocket(CMMSocketImpl *sk_,
                 CMMSocketSender *sendr_, 
                 CMMSocketReceiver *recvr_, 
                 struct net_interface local_iface_, 
                 struct net_interface remote_iface_,
                 int accepted_sock)
    : sk(sk_), sendr(sendr_), recvr(recvr_), 
      local_iface(local_iface_), remote_iface(remote_iface_),
      csock_sendr(NULL), csock_recvr(NULL)
{
    assert(sk && sendr && recvr);
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
    if (osfd > 0) {
	shutdown(osfd, SHUT_RDWR);
    }

    if (csock_sendr) {
	csock_sendr->stop();
    }
    if (csock_recvr) {
	csock_recvr->stop();
    }
    delete csock_sendr;
    delete csock_recvr;

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
	csock_sendr = new CSocketSender(this);
	csock_recvr = new CSocketReceiver(this);
	csock_sendr->start();
	csock_recvr->start();
    }
}

void 
CSocket::send(CMMSocketRequest req)
{
    csock_sendr->enqueue(req);
}

void
CSocket::remove()
{
    sk->csock_map->delete_csock(this);
}
