#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <linux/un.h>
#include "libcmm_ipc.h"
#include <vector>
using std::vector;
#include "debug.h"

#include "libcmm_external_ipc.h"

int open_scout_socket()
{
    int sock = socket(PF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket");
        return -1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(&addr.sun_path[1], SCOUT_CONTROL_MQ_NAME,
            UNIX_PATH_MAX - 2);
    int rc = connect(sock, (struct sockaddr *)&addr, sizeof(addr));
    if (rc < 0) {
        perror("connect");
        dbgprintf_always("Failed to connect to scout IPC socket\n");
        close(sock);
        return -1;
    }
    return sock;
}

bool get_local_interfaces(vector<net_interface>& ifaces)
{
    int sock = open_scout_socket();
    if (sock < 0) {
        return -1;
    }
    
    struct cmm_msg msg;
    msg.opcode = CMM_MSG_GET_IFACES;
    msg.data.pid = getpid();
    int rc = write(sock, &msg, sizeof(msg));
    if (rc != sizeof(msg)) {
        dbgprintf_always("Failed to send get_ifaces request to scout\n");
        return false;
    }
    
    do {
        rc = recv(sock, &msg, sizeof(msg), MSG_WAITALL);
        if (rc != sizeof(msg)) {
            dbgprintf_always("Failed to receive ifaces from scout\n");
            return false;
        }
        if (msg.data.iface.ip_addr.s_addr != 0) {
            ifaces.push_back(msg.data.iface);
        }
    } while (msg.data.iface.ip_addr.s_addr != 0);
    
    close(sock);
    return true;
}