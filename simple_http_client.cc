#include <libcmm.h>
#include <stdio.h>
#include <errno.h>
#include <assert.h>
#include <string.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static void handle_error(bool fail, const char *msg)
{
    if (fail) {
        fprintf(stderr, "Failed: %s (%s)\n", msg, strerror(errno));
        exit(1);
    }
}

int main(int argc, char *argv[])
{
    if (argc != 3) {
        fprintf(stderr, "usage: %s <ip address> <port>\n",
                argv[0]);
        exit(1);
    }
    
    char *ip = argv[1];
    short port = atoi(argv[2]);
    printf("Will connect to %s:%d\n", ip, port);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    inet_aton(ip, &addr.sin_addr);
    addr.sin_port = htons(port);

    socklen_t addrlen = sizeof(addr);

    mc_socket_t sock = cmm_socket(PF_INET, SOCK_STREAM, 0);
    handle_error(sock < 0, "failed to create socket");

    int rc = cmm_connect(sock, (struct sockaddr *) &addr, addrlen);
    handle_error(rc < 0, "failed to connect");

    char request[128];
    snprintf(request, sizeof(request) - 1, "GET http://%s:%d/ HTTP/1.0\r\n\r\n", ip, port);
    printf("Request: %s\n", request);
    
    rc = cmm_write(sock, request, strlen(request), 
                   CMM_LABEL_ONDEMAND | CMM_LABEL_SMALL, 
                   NULL, NULL);
    handle_error(rc != (int) strlen(request), "Failed to send bytes");

    char response[4096];
    int bytes_recvd = 0;
    while (rc > 0) {
        rc = cmm_read(sock, response, sizeof(response) - 1 - bytes_recvd, NULL);
        handle_error(rc < 0, "Failed to receive bytes");
        bytes_recvd += rc;
    }
    assert(bytes_recvd < (int) sizeof(response));
    response[bytes_recvd] = '\0';
    fprintf(stderr, "Received %d bytes\n%s\n", bytes_recvd, response);
    
    cmm_close(sock);
}
