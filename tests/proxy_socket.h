#ifndef proxy_socket_h_incl_iaguveoruwvheb
#define proxy_socket_h_incl_iaguveoruwvheb

#include <netinet/in.h>
#ifdef ANDROID
typedef uint16_t in_port_t;
#endif

typedef bool (*chunk_proc_fn_t)(int, char *, size_t, void *);

void proxy_lines_until_closed(int client_fd, int server_fd, 
                              chunk_proc_fn_t chunk_proc, void *arg);
int start_proxy_thread(pthread_t *proxy_thread, in_port_t proxy_port, in_port_t server_port, 
                       chunk_proc_fn_t chunk_proc, void *arg);
void stop_proxy_thread(pthread_t proxy_thread);

#endif
