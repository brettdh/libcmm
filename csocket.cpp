#include "cmm_socket.private.h"
#include "csocket.h"
#include "debug.h"
#include "timeops.h"
#include "cmm_socket_control.h"
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include "csocket_sender.h"
#include "csocket_receiver.h"
#include "csocket_mapping.h"
#include "libcmm_shmem.h"
#include <functional>
#include "common.h"
using std::max;

#ifdef CMM_UNIT_TESTING
CSocket::CSocket(struct net_interface local_iface_,
                 struct net_interface remote_iface_)
    : local_iface(local_iface_), remote_iface(remote_iface_)
{
}
#else
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
    : oserr(0), sk(sk_),
      local_iface(local_iface_), remote_iface(remote_iface_),
      stats(local_iface, remote_iface),
      csock_sendr(NULL), csock_recvr(NULL), connected(false),
      accepting(false),
      irob_indexes(local_iface_.labels), busy(false)
{
    pthread_mutex_init(&csock_lock, NULL);
    pthread_cond_init(&csock_cv, NULL);

    //TIME(last_fg);
    last_fg.tv_sec = last_fg.tv_usec = 0;

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
        //connected = true;
        accepting = true;
        // XXX: need to wait until the end-to-end library-level connect 
        //  handshake completes
        
        // XXX: should I do it here too?
        //sk->set_all_sockopts(osfd);
    }
    
    int on = 1;
    int rc;

    /* Make sure that this socket is TCP_NODELAY for good performance */
    rc = setsockopt (osfd, IPPROTO_TCP, TCP_NODELAY, 
                     (char *) &on, sizeof(on));
    if (rc < 0) {
        dbgprintf("Cannot make socket TCP_NODELAY");
    }

    // we want our CSockets to die immediately on shutdown/close, and
    //  since we have our own ACKs, it doesn't matter if TCP discards
    //  the data.  We'll double-check that it arrived.
    // Further, this will avoid any nasty retransmissions on dead networks.
    /*
    struct linger ls;
    ls.l_onoff = 1;
    ls.l_linger = 0;
    rc = setsockopt(osfd, SOL_SOCKET, SO_LINGER,
                    (char*)&ls, sizeof(ls));
    if (rc < 0) {
        dbgprintf("Failed to set SO_LINGER\n");
    }
    // XXX: We actually do want to wait for control messages to finish.
    */

    int window = 0;
    socklen_t len = sizeof(window);
    rc = getsockopt(osfd, SOL_SOCKET, SO_SNDBUF, (char *)&window, &len);
    if (rc < 0) {
        dbgprintf("Couldn't get SNDBUF size: %s\n", strerror(errno));
    } else {
        dbgprintf("New csocket osfd %d SNDBUF %d\n",
                  osfd, window);
    }

    /* HERE BE DRAGONS
     *
     * Messing with the socket buffer can screw with the striping discipline,
     *  which is illustrated by the crowded hotspot scenario.
     * Why I think this is the case:
     *   When CSocket socket buffers are small enough, sender threads on both 
     *    networks block on socket buffer space.  The socket buffers drain
     *    at different rates, so BG traffic is naturally striped according to 
     *    the networks' relative bandwidths.
     *   When CSocket socket buffers are large enough, sender threads don't 
     *    block on buffer space, but the buffers will still drain at about
     *    the right rates, I think.
     *   When CSocket socket buffers are in the middle, one sender will block
     *    while the other does not.  The effect of this is that a fixed amount
     *    of data gets sent onto the fast network, and then the sender thread
     *    on the slower network shoves more data into its socket buffer,
     *    tilting the striping ratio in the wrong direction.
     *   The Right Way to fix this is to explicitly manage the striping ratios,
     *    as I believe prior work has done, but leaving the socket buffer
     *    sizes at the default 16K appears to have the desired effect,
     *    within an acceptable range of overhead.
     *   This should perhaps be more carefully verified, but the short-term
     *    fix appears to work for our current purposes.
     */
    // window = 131072;
//      /* window = 2 * 1024 * 1024; */
//      rc = setsockopt(osfd, SOL_SOCKET, SO_SNDBUF, (char *) &window, 
//                      sizeof(window));
//      if(rc < 0) dbgprintf("failed to set SNDBUF: %s\n", strerror(errno));
//      rc = setsockopt(osfd, SOL_SOCKET, SO_RCVBUF, (char *) &window, 
//                      sizeof(window));
//      if(rc < 0) dbgprintf("failed to set SNDBUF: %s\n", strerror(errno));

//      rc = getsockopt(osfd, SOL_SOCKET, SO_SNDBUF, (char *)&window, &len);
//      if (rc < 0) {
//          dbgprintf("Couldn't get SNDBUF size: %s\n", strerror(errno));
//      } else {
//          dbgprintf("New csocket osfd %d - increased SNDBUF to %d\n",
//                    osfd, window);
//      }
}

CSocket::~CSocket()
{
    dbgprintf("CSocket %d is being destroyed\n", osfd);
    if (osfd > 0) {
        /* if it's a real open socket */
        assert(csock_sendr == NULL && csock_recvr == NULL);
        ipc_remove_csocket(iface_pair(local_iface.ip_addr,
                                      remote_iface.ip_addr), osfd);
        close(osfd);
    }    
}

int
CSocket::phys_connect()
{
    if (accepting) {
        // this was created by accept() in the listener thread
        return wait_until_connected();
    }

    struct sockaddr_in local_addr, remote_addr;
    
    // XXX-TODO: don't assume it's an inet socket

    local_addr.sin_family = AF_INET;
    local_addr.sin_addr = local_iface.ip_addr;
    local_addr.sin_port = 0;
    
    remote_addr.sin_family = AF_INET;
    remote_addr.sin_addr = remote_iface.ip_addr;
    remote_addr.sin_port = sk->remote_listener_port;

    try {
        int rc = bind(osfd, (struct sockaddr *)&local_addr, 
                      sizeof(local_addr));
        if (rc < 0) {
            oserr = errno;
            perror("bind");
            dbgprintf("Failed to bind osfd %d to %s:%d\n",
                      osfd, inet_ntoa(local_addr.sin_addr), 
                      ntohs(local_addr.sin_port));
            close(osfd);
            throw -1;
        }
        dbgprintf("Successfully bound osfd %d to %s:%d\n",
                  osfd, inet_ntoa(local_addr.sin_addr), 
                  ntohs(local_addr.sin_port));
    
        rc = connect(osfd, (struct sockaddr *)&remote_addr, 
                     sizeof(remote_addr));
        if (rc < 0) {
            oserr = errno;
            perror("connect");
            dbgprintf("Failed to connect osfd %d to %s:%d\n",
                      osfd, inet_ntoa(remote_addr.sin_addr), 
                      ntohs(remote_addr.sin_port));
            close(osfd);
            throw -1;
        }

        //if (!sk->isLoopbackOnly()) {
        struct CMMSocketControlHdr hdr;
        memset(&hdr, 0, sizeof(hdr));
        hdr.type = htons(CMM_CONTROL_MSG_NEW_INTERFACE);
        hdr.send_labels = 0;
        hdr.op.new_interface.ip_addr = local_iface.ip_addr;
        hdr.op.new_interface.labels = htonl(local_iface.labels);
        hdr.op.new_interface.bandwidth_down = htonl(local_iface.bandwidth_down);
        hdr.op.new_interface.bandwidth_up = htonl(local_iface.bandwidth_up);
        hdr.op.new_interface.RTT = htonl(local_iface.RTT);
        rc = send(osfd, &hdr, sizeof(hdr), 0);
        if (rc != sizeof(hdr)) {
            oserr = errno;
            perror("send");
            dbgprintf("Failed to send interface info\n");
            close(osfd);
            throw -1;
        }

        rc = recv(osfd, &hdr, sizeof(hdr), 0);
        if (rc != sizeof(hdr)) {
            if (rc < 0) {
                perror("recv");
                oserr = errno;
            } else {
                dbgprintf("Connection shutdown.\n");
                oserr = ECONNRESET;
            }
            dbgprintf("Failed to recv confirmation (HELLO)\n");
            close(osfd);
            throw -1;
        }
        if (ntohs(hdr.type) != CMM_CONTROL_MSG_HELLO) {
            dbgprintf("Received unexpected message in place of CSocket connect confirmation: %s\n",
                      hdr.describe().c_str());
            oserr = ECONNRESET;
            close(osfd);
            throw -1;
        }
        //}
    } catch (int rc) {
        PthreadScopedLock lock(&csock_lock);
        osfd = -1;
        pthread_cond_broadcast(&csock_cv);
        return rc;
    }


    {
        PthreadScopedLock lock(&csock_lock);
        connected = true;
        pthread_cond_broadcast(&csock_cv);
    }

    return 0;
}

void
CSocket::send_confirmation()
{
    struct CMMSocketControlHdr hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.type = htons(CMM_CONTROL_MSG_HELLO);
    int rc = send(osfd, &hdr, sizeof(hdr), 0);
    if (rc != sizeof(hdr)) {
        perror("send");
        dbgprintf("Error sending confirmation (HELLO)\n");

        PthreadScopedLock lock(&csock_lock);
        close(osfd);
        osfd = -1;
        pthread_cond_broadcast(&csock_cv);
        return;
    }

    PthreadScopedLock lock(&csock_lock);
    connected = true;
    pthread_cond_broadcast(&csock_cv);
}

bool
CSocket::is_connected()
{
    PthreadScopedLock lock(&csock_lock);
    return connected;
}

int
CSocket::wait_until_connected()
{
    PthreadScopedLock lock(&csock_lock);
    while (!connected && osfd != -1) {
        pthread_cond_wait(&csock_cv, &csock_lock);
    }

    if (osfd == -1) {
        return -1;
    }
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

/* must not be holding sk->scheduling_state_lock. */
bool 
CSocket::matches(u_long send_labels)
{
    return sk->csock_map->csock_matches(this, send_labels);
}

/* must not be holding sk->scheduling_state_lock. */
bool CSocket::is_fg()
{
    return (matches(CMM_LABEL_ONDEMAND|CMM_LABEL_SMALL) ||
            matches(CMM_LABEL_ONDEMAND|CMM_LABEL_LARGE));
}

// must be holding scheduling_state_lock
// return true iff the csocket is busy sending app data
bool CSocket::is_busy()
{
    if (busy ||
        !irob_indexes.new_irobs.empty() || 
        !irob_indexes.new_chunks.empty()) {
        return true;
    }

    int rc = get_unsent_bytes(osfd);
    if (rc > 0) {
        return true;
    }
    return false;
}

u_long
CSocket::bandwidth()
{
    u_long bw_est;
    if (stats.get_estimate(NET_STATS_BW_UP, bw_est)) {
        return bw_est;
    } else {
        return iface_bandwidth(local_iface, remote_iface);
    }
}

double CSocket::RTT()
{
    u_long latency_est;
    if (stats.get_estimate(NET_STATS_LATENCY, latency_est)) {
        return (double)(latency_est * 2);
    } else {
        return iface_RTT(local_iface, remote_iface);
    }
}

struct timespec 
CSocket::retransmission_timeout()
{
    // XXX: with a higher rto, the mobicom-intermittent benchmark is
    // timing out, so try this for now.
    struct timespec dumb_rto = {3, 0};
    return dumb_rto;

    // have a fairly high floor on this so that we don't
    //  flood the socket with spurious retransmissions
    struct timespec min_rto = {30, 0};
    struct timespec default_rto = {120, 0};
    
    int bufsize = 0;
    socklen_t len = sizeof(bufsize);
    int rc = getsockopt(osfd, SOL_SOCKET, SO_SNDBUF, &bufsize, &len);
    if (rc < 0) {
        dbgprintf("Failed to get socket send buffer size: %s\n", strerror(errno));
        dbgprintf("   returning default RTO\n");
        return default_rto;
    }

    u_long bw = bandwidth();
    if (bw == 0) {
        return default_rto;
    }
    u_long rto = ((bufsize / bandwidth()) + 2 * (u_long)RTT()) * 2;
    struct timeval tv = convert_to_timeval(rto);
    struct timespec ts_rto = {tv.tv_sec, tv.tv_usec * 1000};

    if (ts_rto.tv_sec < min_rto.tv_sec) {
        return min_rto;
    }
    return ts_rto;

    /*
    struct tcp_info info;
    socklen_t len = sizeof(info);
    struct protoent *pe = getprotobyname("TCP");
    int rc = -1;
    if (pe) {
        rc = getsockopt (osfd, pe->p_proto, TCP_INFO, &info, &len);
        if (rc == 0) {
            long int usecs = info.tcpi_rto;
            ret.tv_sec = usecs / 1000000;
            ret.tv_nsec = (usecs - (ret.tv_sec*1000000)) * 1000;
        } else {
            dbgprintf("getsockopt failed for TCP_INFO: %s\n",
                      strerror(errno));
        }
    } else {
        dbgprintf("getprotoent failed for TCP: %s\n",
                  strerror(errno));
    }
    if (rc < 0) {
        dbgprintf("Cannot read tcpi_rto; making a lazy guess\n");
        //TODO: more accurate guess?
    }
    dbgprintf("Retransmission timeout for csock %d is %ld.%09ld\n",
              osfd, ret.tv_sec, ret.tv_nsec);
    */
}

long int
CSocket::tcp_rto()
{
    struct tcp_info info;
    memset(&info, 0, sizeof(info));
    socklen_t len = sizeof(info);
    struct protoent *pe = getprotobyname("TCP");
    int rc = -1;
    if (pe) {
        rc = getsockopt (osfd, pe->p_proto, TCP_INFO, &info, &len);
        if (rc == 0) {
            return info.tcpi_rto;
        } else {
            dbgprintf("getsockopt failed for TCP_INFO: %s\n",
                      strerror(errno));
        }
    } else {
        dbgprintf("getprotoent failed for TCP: %s\n",
                  strerror(errno));
    }

    dbgprintf("Cannot read tcpi_rto; returning -1\n");
    return -1;
}

void
CSocket::print_tcp_rto()
{
    // getprotobyname isn't implemented on Android (1.5, at least)
    //  I could fake it, but this isn't crucial.
#ifndef ANDROID
    dbgprintf("TCP RTO for csock %d is %ld\n", osfd, tcp_rto());
#endif
}

//#define useconds(tv) ((tv).tv_sec*1000000 + (tv).tv_usec)

ssize_t
CSocket::trickle_chunksize()/*struct timeval time_since_last_fg,
                              struct timeval bg_wait_time)*/
{
    const long int max_tolerable_fg_delay = 50; //ms
    ssize_t max_chunksize = (bandwidth() * max_tolerable_fg_delay) / 1000;
    //ssize_t min_chunksize = 64;
    /*
    ssize_t chunksize = min_chunksize * (1 << (useconds(time_since_last_fg) /
                                               useconds(sk->bg_wait_time())*2));
    if (chunksize < 0) {
        chunksize = max_chunksize;
    }
    */
    //ssize_t chunksize = max(chunksize, min_chunksize);
    ssize_t chunksize = max_chunksize;
    return chunksize;
}

void
CSocket::update_last_fg()
{
    //struct timeval now;
    //struct timeval diff;
    //TIME(now);

    //TIMEDIFF(last_fg, now, diff);
    //last_fg = now;
    TIME(last_fg);
    ipc_update_fg_timestamp(CSocketPtr(this));
}

struct net_interface
CSocket::bottleneck_iface()
{
    if (local_iface.bandwidth_up > remote_iface.bandwidth_down) {
        return remote_iface;
    } else {
        return local_iface;
    }
}
#endif
