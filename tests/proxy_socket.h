#ifndef proxy_socket_h_incl_iaguveoruwvheb
#define proxy_socket_h_incl_iaguveoruwvheb

typedef bool (*line_proc_fn_t)(int, char *);

void proxy_lines_until_closed(int client_fd, int server_fd, 
                              line_proc_fn_t line_proc);
int start_proxy_thread(pthread_t *proxy_thread, short proxy_port, short server_port, 
                       line_proc_fn_t line_proc);

#endif
