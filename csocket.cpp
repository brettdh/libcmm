#include "cmm_socket.private.h"
#include "csocket.h"
#include "debug.h"

class CSocketSender : public CMMSocketScheduler<struct CMMSocketRequest> {
  public:
    explicit CSocketSender(CSocket *csock_);
  private:
    CSocket *csock;
    
    void send_header(struct CMMSocketRequest req);
    void do_begin_irob(struct CMMSocketRequest req);
    void do_end_irob(struct CMMSocketRequest req);
    void do_irob_chunk(struct CMMSocketRequest req);
};

class CSocketReceiver : public CMMSocketScheduler<struct CMMSocketControlHdr> {
  public:
    explicit CSocketReceiver(CSocket *csock_);
  protected:
    virtual void Run();
  private:
    CSocket *csock;

    void pass_header(struct CMMSocketControlHdr hdr);
    void do_begin_irob(struct CMMSocketControlHdr hdr);
    void do_irob_chunk(struct CMMSocketControlHdr hdr);
};


CSocketSender::CSocketSender(CSocket *csock_)
    : csock(csock_)
{
    handle(CMM_CONTROL_MSG_BEGIN_IROB, &CSocketSender::do_begin_irob);
    handle(CMM_CONTROL_MSG_END_IROB, &CSocketSender::do_end_irob);
    handle(CMM_CONTROL_MSG_IROB_CHUNK, &CSocketSender::do_irob_chunk);
    handle(CMM_CONTROL_MSG_NEW_INTERFACE, &CSocketSender::send_header);
    handle(CMM_CONTROL_MSG_DOWN_INTERFACE, &CSocketSender::send_header);
    handle(CMM_CONTROL_MSG_ACK, &CSocketSender::send_header);
}

void
CSocketSender::send_header(struct CMMSocketRequest req)
{
    struct CMMSocketControlHdr& hdr = req.hdr;
    int rc = send(csock->osfd, &hdr, sizeof(hdr), 0);
    if (rc != sizeof(hdr)) {
        /* give this request back to the send scheduler */
        csock->sendr->enqueue(req);
        throw CMMControlException("Socket error", req);
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
        try {
            send_header(hdr);
            int rc = send(csock->osfd, deps, datalen, 0);
            if (rc != datalen) {
                csock->sendr->enqueue(req);
                throw CMMControlException("Socket error", req);
            }
        } catch (CMMControlException& e) {
            e.hdr.op.begin_irob.deps = deps;
            throw;
        }
        /* this buffer is no longer needed, so delete it */
        delete [] deps;
    } else {
        send_header(hdr);
    }
    csock_sendr->signal_completion(req.requester_tid, 0);
}

void CSocketSender::do_end_irob(struct CMMSocketRequest req)
{
    send_header(req.hdr);
    csock_sendr->signal_completion(req.requester_tid, 0);
}

void
CSocketSender::do_irob_chunk(struct CMMSocketRequest req)
{
    struct CMMSocketControlHdr& hdr = req.hdr;
    assert(ntohs(hdr.type) == CMM_CONTROL_MSG_IROB_CHUNK);
    int datalen = ntohl(hdr.op.irob_chunk.datalen);
    if (datalen <= 0) {
        throw CMMControlException("Expected data with header, got none", hdr);
    }
    
    char *buf = hdr.op.irob_chunk.data;
    hdr.op.irob_chunk.data = NULL;
    
    try {
        send_header(hdr);
        int rc = send(csock->osfd, buf, datalen, 0);
        if (rc != datalen) {
            csock->sendr->enqueue(req);
            throw CMMControlException("Socket error", hdr);
        }
    } catch (CMMControlException& e) {
        e.hdr.op.irob_chunk.data = buf;
        throw;
    }
    /* notice that we don't delete the data buffer here, 
     * since it is an un-ACK'd part of an IROB. 
     * It will be cleaned up later when the ACK is received. */

    csock_sendr->signal_completion(req.requester_tid, datalen);
}

CSocketReceiver::CSocketReceiver(CSocket *csock_)
    : csock(csock_)
{
    handle(CMM_CONTROL_MSG_BEGIN_IROB, &CSocketReceiver::do_begin_irob);
    handle(CMM_CONTROL_MSG_END_IROB, &CSocketReceiver::pass_header);
    handle(CMM_CONTROL_MSG_IROB_CHUNK, &CSocketReceiver::do_irob_chunk);
    handle(CMM_CONTROL_MSG_NEW_INTERFACE, &CSocketReceiver::pass_header);
    handle(CMM_CONTROL_MSG_DOWN_INTERFACE, &CSocketReceiver::pass_header);
    handle(CMM_CONTROL_MSG_ACK, &CSocketReceiver::pass_header);
}

void
CSocketReceiver::Run(void)
{
    while (1) {
        struct CMMSocketControlHdr hdr;
        
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(csock->osfd, &readfds);
        rc = select(csock->osfd + 1, &readfds, NULL, NULL, NULL, NULL);
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
        int rc = recv(osfd, (char*)deps, datalen, 0);
        if (rc != datalen) {
            if (rc < 0) {
                dbgprintf("Error %d on socket %d\n", errno, csock->osfd);
            } else {
                dbgprintf("Expected %d bytes after header, received %d\n", 
                          datalen, rc);
            }
            delete [] deps;
            throw CMMControlException("Socket error", hdr);
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
        throw CMMControlException("Expected data with header, got none", hdr);
    }
    
    char *buf = new char[datalen];
    int rc = recv(osfd, buf, datalen, 0);
    if (rc != datalen) {
        if (rc < 0) {
            dbgprintf("Error %d on socket %d\n", errno, csock->osfd);
        } else {
            dbgprintf("Expected %d bytes after header, received %d\n", 
                      datalen, rc);
        }
        delete [] buf;
        throw CMMControlException("Socket error", hdr);
    }

    hdr.op.irob_chunk.data = buf;
    pass_header(hdr);
}

CSocket::CSocket(CMMSocketImpl *sk_,
                 CMMSocketSender *sendr_, 
                 CMMSocketReceiver *recvr_, 
                 struct net_interface local_iface_, 
                 struct net_interface remote_iface_,
                 int accepted_sock = -1)
    : sk(sk_), sendr(sendr_), recvr(recvr_), 
      local_iface(local_iface_), remote_iface(remote_iface_)
{
    assert(sk);
    if (accepted_sock == -1) {
        osfd = socket(sk->sock_family, sk->sock_type, sk->sock_protocol);
        if (osfd < 0) {
            /* out of file descriptors or memory at this point */
            throw osfd;
        }
        
        sk->set_all_sockopts(osfd);
        int rc = phys_connect();
        if (rc < 0) {
            if (errno==EINPROGRESS || errno==EWOULDBLOCK) {
                //is this what we want for the 'send', 
                //i.e wait until the sock is conn'ed.
                errno = EAGAIN;
            } else {
                perror("connect");
                close(osfd);
                throw rc;
            }
        }
    } else {
        osfd = accepted_sock;
    }

    csock_sendr = new CSocketSender(this);
    csock_recvr = new CSocketReceiver(this);
    csock_sendr->start();
    csock_recvr->start();
}

CSocket::~CSocket()
{
    if (osfd > 0) {
	/* if it's a real open socket */
	close(osfd);
    }
    /* TODO: clean up threads */
}

void 
CSocket::send(struct CMMSocketControlHdr hdr)
{
    csock_sendr->enqueue(hdr);
}

int
CSocket::phys_connect(void)
{
    struct sockaddr_in local_addr, remote_addr;

    local_addr.sin_family = AF_INET;
    local_addr.sin_addr = local_iface.ip_addr;
    local_addr.sin_port = 0;

    remote_addr.sin_family = AF_INET;
    remote_addr.sin_addr = remote_iface.ip_addr;
    remote_addr.sin_port = sk->remote_listener_port;

    int rc = bind(osfd, (struct sockaddr *)&local_addr, sizeof(local_addr));
    if (rc < 0) {
        return rc;
    }
    return connect(osfd, (struct sockaddr *)&remote_addr, sizeof(remote_addr));
}


class LabelMatch {
  public:
    LabelMatch(u_long label_) : label(label_) {}
    virtual bool operator()(CSocket *csock) = 0;
  protected:
    u_long label;
};

class LocalLabelMatch : public LabelMatch {
  public:
    virtual bool operator()(CSocket *csock) {
        return (label == 0) || (csock->local_iface.labels & label);
    }
};

class RemoteLabelMatch : public LabelMatch {
  public:
    virtual bool operator()(CSocket *csock) {
        return (label == 0) || (csock->remote_iface.labels & label);
    }
};

class BothLabelsMatch : public LabelMatch {
  public:
    BothLabelsMatch(u_long send_label, u_long recv_label)
        : LabelMatch(0), /* ignored */
          local_match(send_label), remote_match(recv_label) {}
    virtual bool operator()(CSocket *csock) {
        return local_match(csock) && remote_match(csock);
    }
  private:
    LocalLabelMatch local_match;
    RemoteLabelMatch local_match;
};
    
CSocket *
CSockMapping::find_csock(const LabelMatch& pred)
{
    CSocket *csock = NULL;
    CSockSet::const_iterator it = find_if(sk->connected_csocks.begin(), 
                                          sk->connected_csocks.end(), pred);
    if (it == sk->connected_socks.end()) {
        return NULL;
    } else {
        return *it;
    }    
}

/*
CSocket *
CSockMapping::any_csock(void)
{
    // just grab the first connected socket that exists
    if (connected_csocks.empty()) {
        return NULL;
    } else {
        CSocket *csock = *(connected_csocks.begin());
        return csock;
    }
}
*/

CSockMapping::CSockMapping(CMMSocketImpl *sk_)
    : sk(sk_)
{
    /* empty */
}

CSocket *
CSockMapping::csock_with_send_label(u_long label)
{
    return find_csock(SendLabelMatch(label));
}

CSocket *
CSockMapping::csock_with_recv_label(u_long label)
{
    return find_csock(RecvLabelMatch(label));
}

CSocket *
CSockMapping::csock_with_labels(u_long send_label, u_long recv_label)
{
    return find_csock(BothLabelsMatch(send_label, recv_label));
}

class IfaceMatch {
  public:
    IfaceMatch(u_long label_) : label(label_) {}
    bool operator()(struct net_interface iface) {
        return (label == 0) || (iface.labels & label);
    }
  private:
    u_long label;
};

bool
CSockMapping::get_iface(const NetInterfaceList& ifaces, u_long label,
                        struct net_interface& iface)
{
    NetInterfaceList::const_iterator it = find_if(ifaces.begin(), 
                                                  ifaces.end(), 
                                                  IfaceMatch(label));
    if (it != ifaces.end()) {
        iface = *it;
        return true;
    } else {
        return false;
    }
}

bool
CSockMapping::get_local_iface(u_long label, struct net_interface& iface)
{
    return get_iface(sk->local_ifaces, label, iface);
}

bool
CSockMapping::get_remote_iface(u_long label, struct net_interface& iface)
{
    return get_iface(sk->remote_ifaces, label, iface);
}

int
CSockMapping::new_csock_with_labels(u_long send_label, u_long recv_label,
                                    CSocket *& new_csock)
{
    CSocket *csock = csock_with_labels(send_label, recv_label);
    if (csock) {
        new_csock = csock;
        return 0;
    }

    struct net_interface local_iface, remote_iface;
    if (!(get_local_iface(send_label, local_iface) &&
          get_remote_iface(recv_label, remote_iface))) {
        /* one of the desired labels wasn't available */
        return CMM_DEFERRED;
    }

    csock = new CSocket(sk, sk->sendr, sk->recvr, local_iface, remote_iface);

    sk->connected_csocks.insert(csock);
    // to interrupt any select() in progress, adding the new osfd
    printf("Interrupting any selects() in progress to add osfd %d "
           "to multi-socket %d\n",
           csock->osfd, sock);
    signal_selecting_threads();
    
    new_csock = csock;
    return 0;
}

class AddrMatch {
  public:
    AddrMatch(struct in_addr addr_) : addr(addr_) {}
    bool operator()(struct net_interface iface) {
        return addr.s_addr == iface.ip_addr.s_addr;
    }
  private:
    struct in_addr addr;
};

bool
CSockMapping::get_iface(const NetInterfaceList& ifaces, struct in_addr addr,
                        struct net_interface& iface)
{
    NetInterfaceList::const_iterator it = find_if(ifaces.begin(), 
                                                  ifaces.end(), 
                                                  AddrMatch(addr));
    if (it != ifaces.end()) {
        iface = *it;
        return true;
    } else {
        return false;
    }
}

bool
CSockMapping::get_local_iface(struct in_addr addr, struct net_interface& iface)
{
    return get_iface(sk->local_ifaces, addr, iface);
}

bool
CSockMapping::get_remote_iface(struct in_addr addr, 
                               struct net_interface& iface)
{
    return get_iface(sk->remote_ifaces, addr, iface);
}

void
CSockMapping::add_connection(int sock, 
                             struct in_addr local_addr, 
                             struct in_addr remote_addr)
{
    struct net_interface local_iface, remote_iface;
    if (!get_local_iface(local_addr, local_iface) ||
        !get_remote_iface(remote_addr, remote_iface)) {
        assert(0); /* XXX: valid? only creating a CSocket here
                    * if a connection has arrived on this iface pair */
    }
    
    CSocket *new_csock = new CSocket(sk, sk->sendr, sk->recvr, 
                                     local_iface, remote_iface, 
                                     sock);
    sk->connected_csocks.insert(new_csock);
    signal_selecting_threads();
}
