#ifndef substream_h_incl
#define substream_h_incl

typedef int substream_id_t;

/* from the point of view of the proxy */
void begin(substream_id_t id);
bool substream_incoming(int sock);
void add_send(substream_id_t id, const void *buf, size_t len);
void end(substream_id_t id);

#define SUBSTREAM_MAGIC 0xDECAFBAD

struct SubstreamHdr {
    u_long magic; /* magic number in network byte order. */
    substream_id_t id;
    u_long size; /* 0 if this is a begin/end message */
};

#endif
