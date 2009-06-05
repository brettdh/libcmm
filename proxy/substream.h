#ifndef substream_h_incl
#define substream_h_incl

typedef int substream_id_t;

/* From the point of view of the proxy */
int begin(substream_id_t id);
int add_send(substream_id_t id, const void *buf, size_t len);
int end(substream_id_t id);

/* Peek at the socket's incoming data (leaving it there) and
 * return 1 if it contains a SubstreamHdr, or
 * return 0 if it does not, or
 * return -1 if there is an error peeking at the data.
 *
 * Should only be called on a socket ready for reading.
 */
int substream_incoming(int sock);

#define SUBSTREAM_MAGIC 0xDECAFBAD

#define SSHDR_TYPE_BEGIN 0
#define SSHDR_TYPE_DATA 1
#define SSHDR_TYPE_END 2

struct SubstreamHdr {
    u_long magic; /* magic number in network byte order. */
    substream_id_t id;
    u_short type;
    u_long size; /* 0 if this is a begin/end message; *
                  * otherwise, size of data to follow */
};

#endif
