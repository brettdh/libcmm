#include "libcmm_shmem.h"
#include "shmem_test_common.h"
#include "test_common.h"
#include "debug.h"
#include <glib.h>
#include <stdio.h>
#include <sys/socket.h>
#include <linux/un.h>
#include <sys/wait.h>

void set_socket_buffers(int sock)
{
    int window = TEST_SOCKET_BUFSIZE;
    /* window = 2 * 1024 * 1024; */
    int rc = setsockopt(sock, SOL_SOCKET, SO_SNDBUF, (char *) &window, 
                        sizeof(window));
    if(rc < 0) {
        perror("setsockopt");
        fprintf(stderr, "failed to set SNDBUF\n");
        exit(1);
    }
    rc = setsockopt(sock, SOL_SOCKET, SO_RCVBUF, (char *) &window, 
                    sizeof(window));
    if(rc < 0) {
        perror("setsockopt");
        fprintf(stderr, "failed to set RCVBUF\n");
        exit(1);
    }

    socklen_t len = sizeof(window);
    rc = getsockopt(sock, SOL_SOCKET, SO_SNDBUF, (char *) &window, 
                    &len);
    if(rc < 0) {
        perror("setsockopt");
        fprintf(stderr, "failed to get SNDBUF\n");
        exit(1);
    }
    fprintf(stderr, "[PID %d] send buffer: %d bytes\n", getpid(), window);
    len = sizeof(window);
    rc = getsockopt(sock, SOL_SOCKET, SO_RCVBUF, (char *) &window, 
                    &len);
    if(rc < 0) {
        perror("setsockopt");
        fprintf(stderr, "failed to get RCVBUF\n");
        exit(1);
    }
    fprintf(stderr, "[PID %d] receive buffer: %d bytes\n", getpid(), window);
}

void child_process(in_port_t port)
{
    int sock = socket(PF_INET, SOCK_STREAM, 0);
    handle_error(sock < 0, "socket");

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("10.0.0.2");
    addr.sin_port = htons(port);

    int rc = connect(sock, (struct sockaddr *)&addr, sizeof(addr));
    handle_error(rc < 0, "connect");

    ipc_shmem_init(false);
    set_socket_buffers(sock);
    ipc_add_csocket(csocket1, sock);

    char buf[TEST_SOCKET_BUFSIZE*2];
    memset(buf, 42, sizeof(buf));

    fprintf(stderr, "[Sender helper %d] sleeping for 5 seconds.\n",
            getpid());
    struct timespec dur = {5, 0};
    nowake_nanosleep(&dur);
    fprintf(stderr, "[Sender helper %d] writing %zu + %zu bytes.\n",
            getpid(), sizeof(buf), sizeof(buf));
    rc = write(sock, buf, sizeof(buf));
    handle_error(rc != sizeof(buf), "write");
    rc = write(sock, buf, sizeof(buf));
    handle_error(rc != sizeof(buf), "write");

    fprintf(stderr, "[Sender helper %d] removing socket and exiting.\n",
            getpid());
    ipc_remove_csocket(csocket1, sock);
    close(sock);
}

void parent_process(int listen_sock)
{
    int sock = accept(listen_sock, NULL, 0);
    if (sock < 0) {
        perror("accept");
        exit(1);
    }
    close(listen_sock);

    ipc_shmem_init(false);
    set_socket_buffers(sock);
    ipc_add_csocket(csocket1, sock);

    char buf[TEST_SOCKET_BUFSIZE*2];
    memset(buf, 0, sizeof(buf));
    
    fprintf(stderr, "[Receiver helper %d] sleeping for 10 seconds.\n",
            getpid());
    struct timespec dur = {10, 0};
    nowake_nanosleep(&dur); // let the remote send buffer stay full
    fprintf(stderr, "[Receiver helper %d] reading %zu + %zu bytes.\n",
            getpid(), sizeof(buf), sizeof(buf));
    int rc = recv(sock, buf, sizeof(buf), MSG_WAITALL);
    handle_error(rc != sizeof(buf), "recv");
    rc = recv(sock, buf, sizeof(buf), MSG_WAITALL);
    handle_error(rc != sizeof(buf), "recv");

    fprintf(stderr, "[Receiver helper %d] removing socket and exiting.\n",
            getpid());

    ipc_remove_csocket(csocket1, sock);
    close(sock);
}

void test_global_buffer_count()
{
    int sock = socket(PF_INET, SOCK_STREAM, 0);
    if(sock < 0) {
	perror("socket");
	exit(1);
    }
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    in_port_t port = 4242;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    while (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        if (errno == EADDRINUSE) {
            addr.sin_port = htons(++port);
        } else {
            perror("bind");
            exit(1);
        }
    }

    if (listen(sock, 10) < 0) {
        perror("listen");
        exit(1);
    }

    switch(fork()) {
	case 0:
	    close(sock);
	    child_process(port);
	    break;
	case -1:
	    perror("fork");
	    exit(1);
	default:
	    parent_process(sock);
	    break;
    }

}

int main(int argc, char *argv[])
{
    csocket1.reset(new CSocket(iface1, iface2));
    csocket2.reset(new CSocket(iface1, iface3));

    assert(argc >= 2);
    
    const char *cmd = argv[1];
    if (!strcmp(cmd, "test_global_buffer_count")) {
        test_global_buffer_count();
    } else {
        assert(0);
    }

    return 0;
}
