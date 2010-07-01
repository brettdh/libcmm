#include "common.h"

int get_unsent_bytes(int sock)
{
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
    dbgprintf("%d bytes in sndbuf and %d bytes unacked = %d bytes unsent?\n",
              bytes_in_send_buffer, info.tcpi_unacked, unsent_bytes);
    return unsent_bytes;
}
