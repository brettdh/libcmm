#ifndef proxy_socket_h_incl_iaguveoruwvheb
#define proxy_socket_h_incl_iaguveoruwvheb

typedef bool (*chunk_proc_fn_t)(int, char *, size_t, void *);

void proxy_lines_until_closed(int client_fd, int server_fd, 
                              chunk_proc_fn_t chunk_proc, void *arg);
int start_proxy_thread(pthread_t *proxy_thread, short proxy_port, short server_port, 
                       chunk_proc_fn_t chunk_proc, void *arg);
void stop_proxy_thread(pthread_t proxy_thread);

#endif
