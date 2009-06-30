#include "cmm_socket.private.h"
#include "csocket.h"
#include "debug.h"

static void * CSocketReceiver(void *arg);

CSocket::CSocket(CMMSocketReceiver *recvr_, 
                 struct net_interface local_iface_, 
                 struct net_interface remote_iface_,
                 in_port_t remote_listener_port_)
    : recvr(recvr_), local_iface(local_iface_), remote_iface(remote_iface_),
      remote_listener_port(remote_listener_port_)
{
    assert(msock);
    osfd = socket(msock->sock_family, msock->sock_type, msock->sock_protocol);
    if (osfd < 0) {
	/* out of file descriptors or memory at this point */
	throw osfd;
    }

    if (dispatcher.size() == 0) {
        dispatcher[CMM_CONTROL_MSG_BEGIN_IROB] = &CSocket::pass_header_and_data;
        dispatcher[CMM_CONTROL_MSG_END_IROB] = &CSocket::pass_header;
        dispatcher[CMM_CONTROL_MSG_IROB_CHUNK] = &CSocket::pass_header_and_data;
        dispatcher[CMM_CONTROL_MSG_NEW_INTERFACE] = &CSocket::pass_header;
        dispatcher[CMM_CONTROL_MSG_DOWN_INTERFACE] = &CSocket::pass_header;
        dispatcher[CMM_CONTROL_MSG_ACK] = &CSocket::pass_header;
    }

    int rc = pthread_create(&listener, NULL, CSocketReceiver, this);
    if (rc != 0) {
        close(osfd);
        throw rc;
    }
}

CSocket::~CSocket()
{
    if (osfd > 0) {
	/* if it's a real open socket */
	close(osfd);
    }
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
    remote_addr.sin_port = remote_listener_port;

    int rc = bind(osfd, (struct sockaddr *)&local_addr, sizeof(local_addr));
    if (rc < 0) {
        return rc;
    }
    return connect(osfd, (struct sockaddr *)&remote_addr, sizeof(remote_addr));
}

void
CSocket::RunReceiver(void)
{
    while (1) {
        struct CMMSocketControlHdr hdr;
        
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(osfd, &readfds);
        rc = select(osfd + 1, &readfds, NULL, NULL, NULL, NULL);
        if (rc < 0) {
            if (errno == EINTR) {
                continue;
            } else {
                return;
            }
        }
        rc = recv(osfd, &hdr, sizeof(hdr), 0);
        if (rc != sizeof(hdr)) {
            return;
        }
        bool result = dispatch(hdr);
        if (!result) {
            return;
        }
    }
}

bool CSocket::dispatch(struct CMMSocketControlHdr hdr)
{
    short type = ntohs(hdr.type);
    if (dispatcher.find(type) == dispatcher.end()) {
        return unrecognized_control_msg(hdr);
    } else {
        const dispatch_fn_t& fn = dispatcher[type];
        return this->*fn(hdr);
    }
}

bool CSocket::pass_header(struct CMMSocketControlHdr hdr)
{
    recvr->enqueue(hdr);
    return true;
}

bool CSocket::pass_header_and_data(struct CMMSocketControlHdr hdr)
{
    int datalen = -1;
    if (hdr.type == CMM_CONTROL_MSG_BEGIN_IROB) {
        datalen = ntohl(hdr.op.begin_irob.numdeps) * sizeof(irob_id_t);
    } else if (hdr.type == CMM_CONTROL_MSG_IROB_CHUNK) {
        datalen = ntohl(hdr.op.irob_chunk.datalen);
    } else {
        dbgprintf("Unexpected control message type %d\n", hdr.type);
        return false;
    }

    if (datalen <= 0) {
        dbgprintf("Expected data with header, got none\n");
        return false;
    }
    
    char *buf = malloc(datalen);
    assert(buf);

    int rc = recv(osfd, buf, datalen, 0);
    if (rc != datalen) {
        if (rc < 0) {
            dbgprintf("Error %d on socket %d\n", errno, osfd);
        } else {
            dbgprintf("Expected %d bytes after header, received %d\n", 
                      datalen, rc);
        }
        return false;
    }

    if (hdr.type == CMM_CONTROL_MSG_BEGIN_IROB) {
        hdr.op.begin_irob.deps = (irob_id_t*)buf;
    } else if (hdr.type == CMM_CONTROL_MSG_IROB_CHUNK) {
        hdr.op.irob_chunk.data = buf;
    } else assert(0);

    recvr->enqueue(hdr);
    
    return true;
}

bool CSocket::unrecognized_control_msg(struct CMMSocketControlHdr hdr)
{
    dbgprintf("Unrecognized control message type %d\n", hdr.type);
    return false;
}

static void
CSocketReceiver_cleanup(void *arg)
{
    CSocket *csock = (CSocket *)arg;
    delete csock;
}

static void *
CSocketReceiver(void *arg)
{
    pthread_cleanup_push(CSocketReceiver_cleanup, arg);
    CSocket *csock = (CSocket *)arg;
    csock->RunReceiver();
    pthread_cleanup_pop(1);
    return NULL;
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

    csock = new CSocket(sk->recvr, local_iface, remote_iface, 
                        sk->remote_listener_port);
    sk->set_all_sockopts(csock->osfd);
    int rc = csock->phys_connect();
    if (rc < 0) {
        if (errno==EINPROGRESS || errno==EWOULDBLOCK) {
            //is this what we want for the 'send', 
            //i.e wait until the sock is conn'ed.
            errno = EAGAIN;
        } else {
            perror("connect");
            delete csock;
            return CMM_FAILED;
        }
    }

    sk->connected_csocks.insert(csock);
    // to interrupt any select() in progress, adding the new osfd
    printf("Interrupting any selects() in progress to add osfd %d "
           "to multi-socket %d\n",
           csock->osfd, sock);
    signal_selecting_threads();
    
    new_csock = csock;
    return 0;
}

