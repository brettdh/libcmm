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
#include "libcmm_net_restriction.h"

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

void get_ip_string(const struct in_addr *ip_addr, char *ip_string)
{
    inet_ntop(AF_INET, ip_addr, ip_string, INET_ADDRSTRLEN);
}

StringifyIP::StringifyIP(const struct in_addr *addr)
{
    get_ip_string(addr, buffer);
}

const char *
StringifyIP::c_str() const
{
    return buffer;
}

int
modify_bits_string(int value, int mask, const char *str,
                   std::ostringstream& msg)
{
    if (value & mask) {
        msg << str;
        value &= ~mask;
        if (value) {
            msg << ",";
        }
    }
    return value;
}


std::string
describe_labels(u_long send_labels)
{
    static const char *strs[] = {
        "FG", "BG", "SMALL", "LARGE"
    };
    std::ostringstream msg;
    for (int i = 0; i < 4; ++i) {
        int label_mask = 1 << (i + 2); // labels are 4, 8, 16, 32
        send_labels = modify_bits_string(send_labels, label_mask, strs[i], msg);
    }
    
    static const char *net_restriction_strs[] = {
        "WIFI_ONLY", "3G_ONLY"
    };
    for (int i = 0; i < 2; ++i) {
        int label_mask = 1 << (NET_RESTRICTION_LABEL_SHIFT + i);
        send_labels = modify_bits_string(send_labels, label_mask, net_restriction_strs[i], msg);
    }
    return msg.str();
}
