#ifndef proxy_h_incl
#define proxy_h_incl

typedef void (*socket_event_cb_t)(int);

/* Callback invoked when packets arrive that the proxy needs to process.
 * The most transparent application code would just pass bytes from the
 * client to the server with no processing.
 * The first argument is the source socket. 
 * The second argument is the destination socket, or -1 if 
 *   the application should choose the destination. */
typedef int (*process_message_cb_t)(int, int);

typedef struct ProxyConfig {
    const char *server_hostname;
    unsigned short server_port;
    unsigned short listen_port;
    socket_event_cb_t client_sock_connected_cb;
    socket_event_cb_t server_sock_connected_cb;
    socket_event_cb_t client_sock_closed_cb;
    socket_event_cb_t server_sock_closed_cb;
    process_message_cb_t process_message_cb;
} ProxyConfig;

/* return any old client socket, or -1 if none are connected */
int get_any_client_socket(void);

int run_proxy(const ProxyConfig *config);

#ifdef DETAILED_LOGGING
void DPRINT(const char* fmt,...);
#else
#define DPRINT(...)
#endif

/* Always prints. */
void EPRINT(const char* fmt,...);

#endif
