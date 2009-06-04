#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/select.h>
#include <sys/uio.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <stdarg.h>

#include <list>
using std::list;

#include "proxy.h"

#ifdef DETAILED_LOGGING
void DPRINT(const char* fmt,...)
{
    va_list ap;
    va_start(ap,fmt);
    vfprintf(stderr,fmt,ap);
    va_end(ap);
}
#endif

void EPRINT(const char* fmt,...)
{
    va_list ap;
    va_start(ap,fmt);
    vfprintf(stderr,fmt,ap);
    va_end(ap);
}

#define MAX(A,B) (((A)>(B))?(A):(B))

static list<int> client_socklist;


static int setup_server_conn(int s, struct sockaddr *srv_addr,
                             const ProxyConfig *config)
{
    struct sockaddr_in *addr = (struct sockaddr_in *)srv_addr;
    EPRINT("Connecting to server at %s:%d\n",
	   inet_ntoa(addr->sin_addr), ntohs(addr->sin_port));
    int server_sock = socket(PF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) {
	perror("socket");
	return server_sock;
    }
    if (connect(server_sock, srv_addr, (socklen_t)sizeof(*srv_addr)) < 0) {
	perror("connect");
	close(server_sock);
	return -1;
    }
    if (config->server_sock_connected_cb) {
        config->server_sock_connected_cb(server_sock);
    }
    EPRINT("Successfully connected to server\n");
    return server_sock;
}

static void close_client_connection(int client_sock, const ProxyConfig *config)
{
    close(client_sock);
    client_socklist.remove(client_sock);
    assert(config);
    if (config->client_sock_closed_cb) {
        config->client_sock_closed_cb(client_sock);
    }
}

static void close_server_connection(int server_sock, const ProxyConfig *config)
{
    close(server_sock);
    while (!client_socklist.empty()) {
	close(client_socklist.front());
	client_socklist.pop_front();
    }
    assert(config);
    if (config->server_sock_closed_cb) {
        config->server_sock_closed_cb(server_sock);
    }
}

static int running = 1;

static void term_handler(int signum)
{
    EPRINT("Got termination signal\n");
    running = 0;
}


int get_any_client_socket(void)
{
    if (client_socklist.empty()) {
        return -1;
    } else {
        return client_socklist.front();
    }
}


int run_proxy(const ProxyConfig *config)
{
    /* Accept connections from client.
     * Assume for now that only one logical client is connecting.
     * This means that all the incoming traffic will go into the 
     * single outgoing pipe to the server.
     */

    fd_set read_socks, err_socks, active_socks;
    int listen_sock, server_sock = -1;
    int rc, maxfd;
    struct sockaddr_in my_addr, srv_addr;
    struct hostent *he = NULL;

    ProxyConfig local_config;

    if (!config) {
        EPRINT("Error: config cannot be NULL\n");
        return -1;
    }

    /* prevent config from changing later (paranoid?  maybe.) */
    memcpy(&local_config, config, sizeof(ProxyConfig));
    config = &local_config;

    if (!config->server_hostname) {
        EPRINT("Error: config->hostname cannot be NULL\n");
        return -1;
    }
    if (!config->process_message_cb) {
        EPRINT("Error: process_message_cb cannot be NULL\n");
        EPRINT("       (otherwise the proxy is a black hole)\n");
        return -1;
    }

    EPRINT("Proxy starting up\n");
    EPRINT("Listening for client connections on port %d\n",
           config->listen_port);

    memset(&my_addr, 0, sizeof(my_addr));
    my_addr.sin_family = AF_INET;
    my_addr.sin_port = htons(config->listen_port);
    my_addr.sin_addr.s_addr = INADDR_ANY;

    memset(&srv_addr, 0, sizeof(srv_addr));
    srv_addr.sin_family = AF_INET;
    srv_addr.sin_port = htons(config->server_port);

    he = gethostbyname(config->server_hostname);
    if (!he) {
	perror("gethostbyname");
	exit(-1);
    }
    memcpy(&srv_addr.sin_addr.s_addr, he->h_addr, he->h_length);

    listen_sock = socket(PF_INET, SOCK_STREAM, 0);
    if (listen_sock < 0) {
	perror("socket");
	exit(-1);
    }

    int on = 1;
    /* Allow reuse of socket to avoid annoying bind error */
    rc = setsockopt (listen_sock, SOL_SOCKET, SO_REUSEADDR,
		     (char *) &on, sizeof(on));
    if (rc < 0) EPRINT("Cannot reuse socket address");
    
    rc = bind(listen_sock, (struct sockaddr *)&my_addr, 
	      (socklen_t)sizeof(my_addr));
    if (rc < 0) {
	perror("bind");
	exit(-1);
    }

    rc = listen(listen_sock, 5);
    if (rc < 0) {
	perror("listen");
	exit(-1);
    }

    FD_ZERO(&active_socks);

    FD_SET(listen_sock, &active_socks);
    maxfd = listen_sock;

    EPRINT("Listening for connections...\n");

    running = 1;

    signal(SIGINT, term_handler);
    
    while (running) {
	FD_ZERO(&read_socks);
	FD_ZERO(&err_socks);
	read_socks = active_socks;
	err_socks = active_socks;

	rc = select(maxfd + 1, &read_socks, NULL, &err_socks, NULL);
	if (rc < 0) { 
	    if (errno == EINTR) {
		continue;
	    } else {
		perror("select");
		exit(-1);
	    }
	}
	assert(rc > 0);
	DPRINT("\nselect returned %d\n------------------\n", rc);

	/* check for client -> server messages */
	list<int> tmplist(client_socklist);

	int client_sock;
	while (!tmplist.empty()) {
	    client_sock = tmplist.front();
	    tmplist.pop_front();

	    if (FD_ISSET(client_sock, &read_socks)) {
		/* first peek at the stream and find out if the incoming
		 * message is part of a substream */
		
		if (config->process_message_cb(client_sock, server_sock) < 0) {
		    EPRINT("Error on connection %d, closing\n", 
			    client_sock);
		    FD_CLR(client_sock, &active_socks);
		    close_client_connection(client_sock, config);
		    EPRINT("%d clients connected\n", 
			   client_socklist.size());
		    if (client_socklist.empty()) {
			EPRINT("Last client disconnected; "
				"Disconnecting from server\n");
			FD_CLR(server_sock, &active_socks);
			close_server_connection(server_sock, config);
			server_sock = -1;
		    }
		}
		FD_CLR(client_sock, &read_socks);
	    }
	    if (FD_ISSET(client_sock, &err_socks)) {
              close_client_connection(client_sock, config);

		FD_CLR(client_sock, &active_socks);
		FD_CLR(client_sock, &err_socks);
	    }
	}

	/* check for server -> client messages */
	if (server_sock != -1 && FD_ISSET(server_sock, &read_socks)) {
	    if (config->process_message_cb(server_sock, -1) < 0) {
		EPRINT("Failed to handle server reply\n");
		FD_CLR(server_sock, &active_socks);
		FD_SET(server_sock, &err_socks);
	    }
	    FD_CLR(server_sock, &read_socks);
	}
	if (server_sock != -1 && FD_ISSET(server_sock, &err_socks)) {
	    EPRINT("Error on server socket; disconnecting everything\n");
	    close_server_connection(server_sock, config);

	    FD_CLR(server_sock, &err_socks);

	    FD_ZERO(&active_socks);
	    FD_SET(listen_sock, &active_socks);
	    EPRINT("0 clients connected\n");
	    continue;
	}

	/* check for new connections */
	if (FD_ISSET(listen_sock, &read_socks)) {
	    struct sockaddr_in addr;
	    socklen_t len = sizeof(addr);
	    EPRINT("Client connecting\n");
	    int s = accept(listen_sock, (struct sockaddr *)&addr, &len);
	    if (s < 0) {
		perror("accept");
		exit(-1);
	    }

	    maxfd = MAX(maxfd, s);
	    FD_SET(s, &active_socks);
	    client_socklist.push_back(s);

            if (config->client_sock_connected_cb) {
                config->client_sock_connected_cb(s);
            }
	    EPRINT("Accepted client connection from %s:%d\n",
		   inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));
	    EPRINT("%d clients connected\n", client_socklist.size());

	    if (server_sock < 0) {
		server_sock = setup_server_conn(s, 
						(struct sockaddr *)&srv_addr,
                                                config);
		if (server_sock < 0) {
		    EPRINT("Failed to connect to server!\n");
		    exit(-1);
		}
                FD_SET(server_sock, &active_socks);
                maxfd = MAX(maxfd, server_sock);
	    }
	    FD_CLR(listen_sock, &read_socks);
	}
	if (FD_ISSET(listen_sock, &err_socks)) {
	    EPRINT("Error on listen socket\n");
	    exit(-1);
	}
    }

    EPRINT("Cleaning up...\n");
    if (server_sock != -1) {
	close(server_sock);
    }
    while (!client_socklist.empty()) {
	int s = client_socklist.front();
	client_socklist.pop_front();
	close(s);
    }
    close(listen_sock);

    EPRINT("Done.\n");

    return 0;
}
