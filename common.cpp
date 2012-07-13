#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#if defined(LINUX)
#include <linux/sockios.h>
#endif

// tcp.h is temperamental.
#include <netinet/tcp.h>
//#include <linux/tcp.h>

#include <errno.h>
#include "common.h"
#include "debug.h"

int get_unsent_bytes(int sock)
{
#if defined(LINUX)
    int bytes_in_send_buffer = 0;
    int rc = ioctl(sock, SIOCOUTQ, &bytes_in_send_buffer);
    if (rc < 0) {
        return rc;
    }

    struct tcp_info info;
    socklen_t len = sizeof(info);
    rc = getsockopt(sock, IPPROTO_TCP, TCP_INFO, &info, &len);
    if (rc < 0) {
        dbgprintf("Error getting TCP_INFO: %s\n", strerror(errno));
        return bytes_in_send_buffer;
    }

    // subtract the "in-flight" bytes.
    int unsent_bytes = bytes_in_send_buffer - info.tcpi_unacked;
    /*dbgprintf("socket %d: %d bytes in sndbuf and %d bytes unacked = %d bytes unsent?\n",
                sock, bytes_in_send_buffer, info.tcpi_unacked, unsent_bytes);*/
    return unsent_bytes;
#elsif defined (__APPLE__)
    // XXX: SIOCOUTQ not defined.
#else
    return 0;
#endif
}

void get_ip_string(struct in_addr ip_addr, char *ip_string)
{
    char *tmpstr = inet_ntoa(ip_addr);
    strcpy(ip_string, tmpstr);
}
