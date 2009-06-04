#include <sys/uio.h>
#include "tbb/concurrent_hash_map.h"
#include <queue>

#include "substream.h"

class Operation;
typedef std::queue<Operation*> OperationQueue;

class Substream;

#include <boost/shared_ptr.hpp>
typedef boost::shared_ptr<Substream> SubstreamPtr;
typedef tbb::concurrent_hash_map<substream_id_t, SubstreamPtr> SubstreamHash;

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


class Substream {
  public:
    Substream(mc_socket_t sock);

    int sendAll();

    static SubstreamPtr lookup(substream_id_t id_);
    substream_id_t getID();

    ~Substream();
    
  private:
    static SubstreamHash substreams;

    static substream_id_t next_id;

    substream_id_t id;
    mc_socket_t sock;
    OperationQueue ops;
    bool aborted;
};

SubstreamHash Substream::substreams;
substream_id_t Substream::next_id = 0;


Substream::Substream(mc_socket_t sock_)
    : sock(sock_), aborted(false)
{
    do {
        id = next_id++;
        SubstreamHash::accessor ac;
        if (substreams.insert(ac, id)) {
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
