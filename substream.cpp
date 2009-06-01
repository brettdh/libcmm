#include <sys/uio.h>
#include "substream.h"

class Operation {
  public:
    Operation(u_long l, resume_handler_t f, void *a)
        : labels(l), fn(f), arg(a) {}
    virtual int send() = 0;
    virtual ~Operation() {}
  protected:
    u_long labels;
    resume_handler_t fn;
    void *arg;
};

class SendOperation : public Operation {
  public:
    SendOperation(const void *buf, size_t len, int flags,
                  u_long labels, resume_handler_t fn, void *arg);

    int send();
    ~SendOperation();
  private:
    void *buf;
    size_t len;
    int flags;
};

class WritevOperation : public Operation {
  public:
    WritevOperation(const struct iovec *vec, int count,
                    u_long labels, resume_handler_t fn, void *arg);

    int send();
    ~WritevOperation();
  private:
    struct iovec *vec;
    int count;
};


SendOperation::SendOperation(const void *buf_, size_t len_, int flags_,
                             u_long labels, resume_handler_t fn, void *arg)
    : Operation(labels, fn, arg), len(len_), flags(flags_)
{
    buf = new char[len];
    memcpy(buf, buf_, len);
}

SendOperation::~SendOperation()
{
    delete [] buf;
}

int 
SendOperation::send(mc_socket_t sock)
{
    return cmm_send(sock, buf, len, flags, labels, fn, arg);
}


WritevOperation::WritevOperation(const struct iovec *vec_, int count_, 
                                 u_long labels, resume_handler_t fn, void *arg)
    : Operation(labels, fn, arg), count(count_)
{
    vec = new struct iovec[count];
    for (int i = 0; i < count; i++) {
        vec[i].iov_len = vec_[i].iov_len;
        vec[i].iov_base = new char[vec[i].iov_len];
        memcpy(vec[i].iov_base, vec_[i].iov_base, vec[i].iov_len);
    }
}

WritevOperation::~WritevOperation()
{
    for (int i = 0; i < count; i++) {
        delete [] vec[i].iov_base;
    }
    delete [] vec;
}

int 
WritevOperation::send(mc_socket_t sock)
{
    return cmm_writev(sock, vec, count, labels, fn, arg);
}

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
Substream::addSend(const void *buf, size_t len, int flags,
             u_long labels, resume_handler_t fn, void *arg)
{
}

void 
Substream::addWritev(const struct iovec *vec, int count,
               u_long labels, resume_handler_t fn, void *arg)
{
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
        if (rc != CMM_SUCCESS) {
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
