#include "cmm_socket.private.h"
#include "csocket.h"

CSocket::CSocket(CMMSocketImpl *msock_, struct net_interface iface_, 
                 struct net_interface remote_iface_)
    : msock(msock_), local_iface(local_iface_), remote_iface(remote_iface_)
{
    assert(msock);
    osfd = socket(msock->sock_family, msock->sock_type, msock->sock_protocol);
    if (osfd < 0) {
	/* out of file descriptors or memory at this point */
	throw osfd;
    }

    if (dispatcher.size() == 0) {
        dispatcher[CMM_CONTROL_MSG_BEGIN_IROB] = &CSocket::do_begin_irob;
        dispatcher[CMM_CONTROL_MSG_END_IROB] = &CSocket::do_end_irob;
        dispatcher[CMM_CONTROL_MSG_IROB_CHUNK] = &CSocket::do_irob_chunk;
        dispatcher[CMM_CONTROL_MSG_NEW_INTERFACE] = &CSocket::do_new_interface;
        dispatcher[CMM_CONTROL_MSG_DOWN_INTERFACE] = &CSocket::do_down_interface;
        dispatcher[CMM_CONTROL_MSG_ACK] = &CSocket::do_ack;
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
    remote_addr.sin_port = sk->remote_listener_port;

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

/* Second thoughts: which (if not all) of these belong in
 * the scheduler thread? It seems like the individual
 * receiver threads just pass received data to the 
 * scheduler's buffer.
 *
 * On the other hand, maybe these threads should be responsible
 * for ACKing.  But maybe not, since the ACK might be sent on another
 * channel if this one fails. 
 *
 * Yeah, maybe this should all be in the scheduler thread, and
 * the receiver threads can just be dumb(er) producers. */

bool CSocket::do_begin_irob(struct CMMSocketControlHdr hdr)
{
}

bool CSocket::do_end_irob(struct CMMSocketControlHdr hdr)
{
}

bool CSocket::do_irob_chunk(struct CMMSocketControlHdr hdr)
{
}

bool CSocket::do_new_interface(struct CMMSocketControlHdr hdr)
{
}

bool CSocket::do_down_interface(struct CMMSocketControlHdr hdr)
{
}

bool CSocket::do_ack(struct CMMSocketControlHdr hdr)
{
}

bool CSocket::unrecognized_control_msg(struct CMMSocketControlHdr hdr)
{
    return false;
}

static void *
CSocketReceiver(void *arg)
{
    CSocket *csock = (CSocket *)arg;
    csock->RunReceiver();
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

    csock = new CSocket(sk, local_iface, remote_iface);
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

