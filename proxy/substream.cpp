#include <sys/uio.h>
#include "tbb/concurrent_hash_map.h"
#include <queue>
#include <set>
using std::queue; using std::set;

#include "substream.h"

class Operation {
  public:
    Operation(const void *buf, size_t len);
    ssize_t send();
  private:
    void *buf;
    size_t len;
};

Operation::Operation(const void *buf_, size_t len_)
    : len(len_)
{
    buf = new char[len];
    memcpy(buf, buf_, len);
}

Operation::~Operation()
{
    delete [] buf;
}

ssize_t
Operation::send(int sock)
{
    return send(sock, buf, len, 0);
}


typedef queue<Operation*> OperationQueue;

class Substream {
  public:
    Substream(mc_socket_t sock);
    
    void addSend(const void *buf, size_t len);
    int sendAll();
    substream_id_t getID();

    ~Substream();
    
  private:
    substream_id_t id;
    mc_socket_t sock;
    OperationQueue ops;
    bool aborted;
};

typedef tbb::concurrent_hash_map<substream_id_t, Substream*> SubstreamHash;
static SubstreamHash substreams_pending;

/* Proxy is currently single-threaded;
 * These will need to be properly synchronized if we make it
 *   multithreaded later */
static substream_id_t next_id = 0;
static set<substream_id_t> substreams_sent;

Substream::Substream(mc_socket_t sock_)
    : sock(sock_), aborted(false)
{
    do {
        id = next_id++;
        SubstreamHash::accessor ac;
        if (substreams_pending.insert(ac, id)) {
            ac->second = this;
            break;
        } else {
            /* duplicate substream id; VERY unlikely */
            continue;
        }
    } while (1);
}

Substream::~Substream()
{
    while (!ops.empty()) {
        Operation *op = ops.front();
        ops.pop();
        delete op;
    }
}

void 
Substream::addSend(const void *buf, size_t len);
{
    ops.push(new Operation(buf, len));
}

int 
Substream::sendAll()
{
    assert(!aborted);
    /* Send preamble */
    /* TODO */

    /* Send data */
    while (!ops.empty()) {
        Operation *op = ops.front();
        ops.pop();
        int rc = op->send(sock);
        delete op;
        if (rc < 0) {
            aborted = true;
            return rc;
        }
    }
}

SubstreamPtr
Substream::lookup(substream_id_t id_)
{
    SockstreamHash::accessor ac;
    if (sockstreams.find(ac, id_)) {
        return ac->second;
    } else {
        return SubstreamPtr(); /* NULL */
    }
}

substream_id_t
Substream::getID()
{
    return id;
}

int substream_incoming(int sock)
{
    SubstreamHdr hdr;
    memset(&hdr, 0, sizeof(hdr));
    int rc = recv(sock, &hdr, sizeof(hdr), MSG_PEEK);
    if (rc != sizeof(hdr)) {
	return rc;
    }
    return (ntohl(hdr.magic) == SUBSTREAM_MAGIC) ? 1 : 0;
}

int begin(substream_id_t id)
{
    
}

int add_send(substream_id_t id, const void *buf, size_t len)
{
}

int end(substream_id_t id)
{
}
