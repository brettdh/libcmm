#include <arpa/inet.h>
#include <assert.h>
#include <rpc3.h>
#include "proxy.h"

#include <map>
using std::pair; using std::map;


/* from rpc3.c */

typedef struct {
    u_short       type;
    RPC3_SubsysId subsys; 
    u_long        seqno;   
    u_long        datalen; /* Length of data to follow */
    long          rcode;   /* Response code (useful for response type only) */
} RPC3_MessageHdr;

#define STS_KEYSIZE       64
#define STS_HASHSIZE     128

/* These are special messages sent during connection setup.  The first two are
   used always.  The last two are only used for encrypted STS connections */
typedef struct {
    u_long              sec_level;   /* Security level for this connection */
    char                param[STS_KEYSIZE]; /* Used for STS only - DH param */
} Hello;

typedef struct {
    u_long              sec_level;

    /* Used for STS only - DH params */
    char                param[STS_KEYSIZE];
    u_char              signedkey[STS_HASHSIZE];
} HelloResponse;

#define MSG_TYPE_HELLO_REQ   0
#define MSG_TYPE_HELLO_RESP  1
#define MSG_TYPE_STS_REQ     2
#define MSG_TYPE_STS_RESP    3
#define MSG_TYPE_NORMAL_REQ  4
#define MSG_TYPE_NORMAL_RESP 5
#define MSG_TYPE_EXTERNAL    6



#define SERVER_HOSTNAME "localhost"
#define SERVER_PORT 6223
#define PROXY_PORT 16223

/* map seqnos (unswizzled) to client sockets */
static map<u_long, int> outstanding_requests;

/* map from client seqno to server seqno... */
static map<u_long, u_long> server_seqnos;
/* ...and back to client seqno */
static map<u_long, u_long> client_seqnos;

static u_long max_seqno = 0;

/* match up request/response on the same socket. */
int pick_client_sock(u_long seqno)
{
    int client_sock = -1;

    /* Lookup the client socket that made this request. */
    map<u_long, int>::iterator it = outstanding_requests.find(seqno);
    if (it == outstanding_requests.end()) {
	/* Hmm, that connection must have been closed since the
	 * request was made. Okay, just pick any old client socket.*/
        client_sock = get_any_client_socket();
    } else {
	client_sock = it->second;
	outstanding_requests.erase(it);
    }

    return client_sock;
}



static int recv_bytes(int s, void *buf, size_t len)
{
    int rc;
    size_t cnt = 0;
    do {
	DPRINT("About to read %d bytes from socket %d\n", len-cnt, s);
	rc = read(s, ((char*)buf)+cnt, len-cnt);
	if (rc <= 0) {
	    if (rc < 0) {
		perror("read");
	    } else {
		EPRINT("Socket %d abruptly closed\n", s);
	    }
	    return -1;
	}
	DPRINT("==Got %d bytes from socket %d\n", rc, s);
	cnt += rc;
    } while (cnt < len);

    return cnt;
}

/* if to_sock is -1, then this is a server -> client message
 * otherwise, to_sock is the server socket */
int proxy_RPC_request(int from_sock, int to_sock)
{
    struct iovec vect[2];
    RPC3_MessageHdr hdr;
    int rc = recv_bytes(from_sock, (void*)&hdr, sizeof(hdr));
    if (rc != sizeof(hdr)) {
	return -1;
    }

    int numvects = 1;
    vect[0].iov_base = &hdr;
    vect[0].iov_len = sizeof(hdr);
    vect[1].iov_base = NULL;
    vect[1].iov_len = 0;

    int bytes = sizeof(hdr);

    int datalen = ntohl(hdr.datalen);
    if (datalen > 0) {
	void *data = malloc(datalen);
	assert(data);
	rc = recv_bytes(from_sock, data, datalen);
	if (rc != datalen) {
            free(data);
	    return -1;
	}
	vect[1].iov_base = data;
	vect[1].iov_len = datalen;
	bytes += datalen;
	numvects++;
    }

    DPRINT("Got RPC message from source\n");
    DPRINT("   Type: %d\n", ntohs(hdr.type));
    DPRINT("   Seqno: %d\n", ntohl(hdr.seqno));
    DPRINT("   Datalen: %d\n", ntohl(hdr.datalen));

    if (to_sock < 0) {
	/* server response */
	u_long server_seqno = ntohl(hdr.seqno);
    
	if (ntohs(hdr.type) == MSG_TYPE_EXTERNAL) {
	    /* asynchronous server message */
	    DPRINT("RPC ExtMsg seqno %lu\n", server_seqno);
            to_sock = get_any_client_socket();
	} else {
	    /* synchronous server response */
	    u_long client_seqno = client_seqnos[server_seqno];
	    to_sock = pick_client_sock(client_seqno);

	    DPRINT("RPC server response, orig seqno %lu, srv seqno %lu\n",
		   client_seqno, server_seqno);
	    DPRINT("Sending on client socket %d\n", to_sock);

	    client_seqnos.erase(server_seqno);
	    server_seqnos.erase(client_seqno);
	    outstanding_requests.erase(client_seqno);
	    hdr.seqno = htonl(client_seqno);
	}

	if (to_sock < 0) {
	    EPRINT("Server message but no clients connected!\n");
	    free(vect[1].iov_base);
	    return -1;
	}
    } else {
	/* client request */
	/* XXX: we don't handle external messages from the client,
	 * but then again, BlueFS doesn't send any. */

	u_long client_seqno = ntohl(hdr.seqno);
	outstanding_requests[client_seqno] = from_sock;
	u_long server_seqno = client_seqno;
	assert(client_seqno == 0 || client_seqno != max_seqno);
	if (client_seqno < max_seqno) {
	    server_seqno = max_seqno + 1;
	}
	server_seqnos[client_seqno] = server_seqno;
	client_seqnos[server_seqno] = client_seqno;
	max_seqno = server_seqno;

	DPRINT("RPC client request, orig seqno %lu, srv seqno %lu\n",
	       client_seqno, server_seqno);
	/* seqno may have been swizzled, so update it if so */
	hdr.seqno = htonl(server_seqno);
    }

    rc = writev(to_sock, vect, numvects);
    free(vect[1].iov_base);
    if (rc != bytes) {
	return -1;
    }
    DPRINT("Sent RPC message onward\n");
    return 0;
}

template <typename T1, typename T2>
class SecondMatch {
  public:
    SecondMatch(const T2& val_) : val(val_) {}
    bool operator()(const pair<T1, T2>& the_pair)  {
	return the_pair.second == val;
    }
  private:
    T2 val;
};

template <typename MapType, typename Predicate>
void map_remove_if(MapType& the_map, Predicate pred)
{
    typename MapType::iterator it = the_map.begin();
    while (it != the_map.end()) {
	if (pred(*it)) {
	    the_map.erase(it++);
	} else {
	    ++it;
	}
    }
}

void client_connection_closed_cb(int client_sock)
{
    /* remove all seqno swizzlings touched by those outstanding_requests */
    for (map<u_long, int>::iterator it = outstanding_requests.begin();
	 it != outstanding_requests.end(); it++) {
	if (it->second == client_sock) {
	    server_seqnos.erase(client_sock);
	    map_remove_if(client_seqnos,
			  SecondMatch<u_long, u_long>(it->first));
	}
    }
    /* remove all outstanding_request entries pertaining to this socket */
    map_remove_if(outstanding_requests, 
		  SecondMatch<u_long, int>(client_sock));
}

void server_connection_closed_cb(int server_sock)
{
    outstanding_requests.clear();
    client_seqnos.clear();
    server_seqnos.clear();
    max_seqno = 0;
}

int main()
{
    ProxyConfig config = {
        SERVER_HOSTNAME,
        SERVER_PORT,
        PROXY_PORT,
        NULL,   /* client_sock_connected_cb */
        NULL,   /* server_sock_connected_cb */
        client_connection_closed_cb,
        server_connection_closed_cb,
        proxy_RPC_request
    };

    EPRINT("Proxy expecting RPC messages\n");

    return run_proxy(&config);
}
