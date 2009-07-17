#include "cmm_socket.private.h"
#include "csocket.h"
#include "debug.h"
#include "cmm_socket_scheduler.h"
#include "cmm_socket_control.h"
#include "cmm_socket_sender.h"
#include "cmm_socket_receiver.h"
#include <netinet/in.h>
#include <arpa/inet.h>

class CSocketSender : public CMMSocketScheduler<struct CMMSocketRequest> {
  public:
    explicit CSocketSender(CSocket *csock_);
  protected:    
    virtual void dispatch(struct CMMSocketRequest);
    virtual void Finish();
  private:
    CSocket *csock;
    
    void send_header(struct CMMSocketRequest req);
    void do_begin_irob(struct CMMSocketRequest req);
    void do_end_irob(struct CMMSocketRequest req);
    void do_irob_chunk(struct CMMSocketRequest req);
};

void
CSocketSender::dispatch(struct CMMSocketRequest req)
{
    dbgprintf("Sending request\n%s\n", req.describe().c_str());
    CMMSocketScheduler<struct CMMSocketRequest>::dispatch(req);
}

class CSocketReceiver : public CMMSocketScheduler<struct CMMSocketControlHdr> {
  public:
    explicit CSocketReceiver(CSocket *csock_);
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
    handle(CMM_CONTROL_MSG_END_IROB, this, &CSocketSender::do_end_irob);
    handle(CMM_CONTROL_MSG_IROB_CHUNK, this, &CSocketSender::do_irob_chunk);
    handle(CMM_CONTROL_MSG_NEW_INTERFACE, this, &CSocketSender::send_header);
    handle(CMM_CONTROL_MSG_DOWN_INTERFACE, this, &CSocketSender::send_header);
    handle(CMM_CONTROL_MSG_ACK, this, &CSocketSender::send_header);
    handle(CMM_CONTROL_MSG_GOODBYE, this, &CSocketSender::send_header);
}

void
CSocketSender::Finish(void)
{
    csock->remove();
}

void
CSocketSender::send_header(struct CMMSocketRequest req)
{
    struct CMMSocketControlHdr& hdr = req.hdr;
    int rc = send(csock->osfd, &hdr, sizeof(hdr), 0);
    if (rc != sizeof(hdr)) {
        /* give this request back to the send scheduler */
        csock->sendr->enqueue(req);
        throw Exception::make("Socket error", req);
    } else {
	csock->sendr->signal_completion(req.requester_tid, 0);
    }
}

void
CSocketSender::do_begin_irob(struct CMMSocketRequest req)
{
    struct CMMSocketControlHdr& hdr = req.hdr;
    assert(ntohs(hdr.type) == CMM_CONTROL_MSG_BEGIN_IROB);
    int numdeps = ntohl(hdr.op.begin_irob.numdeps);
    if (numdeps > 0) {
        assert(hdr.op.begin_irob.deps);
        int datalen = numdeps * sizeof(irob_id_t);
        irob_id_t *deps = hdr.op.begin_irob.deps;
        hdr.op.begin_irob.deps = NULL; 
        send_header(req);
        int rc = send(csock->osfd, deps, datalen, 0);
        if (rc != datalen) {
            csock->sendr->enqueue(req);
            throw Exception::make("Socket error", req);
        }
        /* this buffer is no longer needed, so delete it */
        delete [] deps;
    } else {
        send_header(req);
    }
    csock->sendr->signal_completion(req.requester_tid, 0);
}

void CSocketSender::do_end_irob(struct CMMSocketRequest req)
{
    send_header(req);
    csock->sendr->signal_completion(req.requester_tid, 0);
}

void
CSocketSender::do_irob_chunk(struct CMMSocketRequest req)
{
    struct CMMSocketControlHdr& hdr = req.hdr;
    assert(ntohs(hdr.type) == CMM_CONTROL_MSG_IROB_CHUNK);
    int datalen = ntohl(hdr.op.irob_chunk.datalen);
    if (datalen <= 0) {
        throw Exception::make("Expected data with header, got none", hdr);
    }
    
    char *buf = hdr.op.irob_chunk.data;
    hdr.op.irob_chunk.data = NULL;
    
    send_header(req);
    int rc = send(csock->osfd, buf, datalen, 0);
    if (rc != datalen) {
	hdr.op.irob_chunk.data = buf;
        csock->sendr->enqueue(req);
        throw Exception::make("Socket error", req);
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
        
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(csock->osfd, &readfds);
        int rc = select(csock->osfd + 1, &readfds, NULL, NULL, NULL);
        if (rc < 0) {
            if (errno == EINTR) {
                continue;
            } else {
                return;
            }
        }
        rc = recv(csock->osfd, &hdr, sizeof(hdr), 0);
        if (rc != sizeof(hdr)) {
            return;
        }
        
        dispatch(hdr);
    }
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
    assert(ntohs(hdr.type) == CMM_CONTROL_MSG_IROB_CHUNK);
    int datalen = ntohl(hdr.op.irob_chunk.datalen);
    if (datalen <= 0) {
        throw Exception::make("Expected data with header, got none", hdr);
    }
    
    char *buf = new char[datalen];
    int rc = recv(csock->osfd, buf, datalen, 0);
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

    hdr.op.irob_chunk.data = buf;
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
}

CSocket::~CSocket()
{
    if (osfd > 0) {
	/* if it's a real open socket */
	close(osfd);
    }
    
    if (csock_sendr) {
	csock_sendr->stop();
    }
    if (csock_recvr) {
	csock_recvr->stop();
    }
    delete csock_sendr;
    delete csock_recvr;
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
    sk->csock_map.delete_csock(this);
}
