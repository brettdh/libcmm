#include "cmm_socket.private.h"

csocket::csocket(CMMSocketImpl *msock_, struct net_interface iface_, 
                 struct net_interface remote_iface_)
    : msock(msock_), local_iface(local_iface_), remote_iface(remote_iface_)
{
    assert(msock);
    osfd = socket(msock->sock_family, msock->sock_type, msock->sock_protocol);
    if (osfd < 0) {
	/* out of file descriptors or memory at this point */
	throw osfd;
    }
}

csocket::~csocket()
{
    if (osfd > 0) {
	/* if it's a real open socket */
	close(osfd);
    }
}

int
csocket::phys_connect(void)
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
    virtual bool operator()(struct csocket *csock) = 0;
  protected:
    u_long label;
};

class LocalLabelMatch : public LabelMatch {
  public:
    virtual bool operator()(struct csocket *csock) {
        return (label == 0) || (csock->local_iface.labels & label);
    }
};

class RemoteLabelMatch : public LabelMatch {
  public:
    virtual bool operator()(struct csocket *csock) {
        return (label == 0) || (csock->remote_iface.labels & label);
    }
};

class BothLabelsMatch : public LabelMatch {
  public:
    BothLabelsMatch(u_long send_label, u_long recv_label)
        : LabelMatch(0), /* ignored */
          local_match(send_label), remote_match(recv_label) {}
    virtual bool operator()(struct csocket *csock) {
        return local_match(csock) && remote_match(csock);
    }
  private:
    LocalLabelMatch local_match;
    RemoteLabelMatch local_match;
};
    
struct csocket *
CSockMapping::find_csock(const LabelMatch& pred)
{
    struct csocket *csock = NULL;
    CSockSet::const_iterator it = find_if(sk->connected_csocks.begin(), 
                                          sk->connected_csocks.end(), pred);
    if (it == sk->connected_socks.end()) {
        return NULL;
    } else {
        return *it;
    }    
}

/*
struct csocket *
CSockMapping::any_csock(void)
{
    // just grab the first connected socket that exists
    if (connected_csocks.empty()) {
        return NULL;
    } else {
        struct csocket *csock = *(connected_csocks.begin());
        return csock;
    }
}
*/

CSockMapping::CSockMapping(CMMSocketImpl *sk_)
    : sk(sk_)
{
    /* empty */
}

struct csocket *
CSockMapping::csock_with_send_label(u_long label)
{
    return find_csock(SendLabelMatch(label));
}

struct csocket *
CSockMapping::csock_with_recv_label(u_long label)
{
    return find_csock(RecvLabelMatch(label));
}

struct csocket *
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
                                    struct csocket *& new_csock)
{
    struct csocket *csock = csock_with_labels(send_label, recv_label);
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

    csock = new struct csocket(sk, local_iface, remote_iface);
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
