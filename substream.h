#ifndef substream_h_incl
#define substream_h_incl

#include "libcmm.h"
#include "libcmm_constraints.h"

#include "tbb/concurrent_hash_map.h"
#include <queue>

class Operation;
typedef std::queue<Operation*> OperationQueue;

class Substream;

#include <boost/shared_ptr.hpp>
typedef boost::shared_ptr<Substream> SubstreamPtr;
typedef tbb::concurrent_hash_map<substream_id_t, SubstreamPtr> SubstreamHash;

class Substream {
  public:
    Substream(mc_socket_t sock);
    
    void addSend(const void *buf, size_t len, int flags,
                 u_long labels, resume_handler_t fn, void *arg);
    void addWritev(const struct iovec *vec, int count,
                   u_long labels, resume_handler_t fn, void *arg);

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

#endif
