#ifndef LIBCMM_NET_RESTRICTION_H
#define LIBCMM_NET_RESTRICTION_H

#include <sys/types.h>

#define NUM_NET_TYPES 2

#define NET_TYPE_WIFI 0x1
#define NET_TYPE_THREEG 0x2

#define NET_RESTRICTION_LABEL_SHIFT 16
#define CMM_LABEL_WIFI_ONLY (NET_TYPE_WIFI << NET_RESTRICTION_LABEL_SHIFT)
#define CMM_LABEL_THREEG_ONLY (NET_TYPE_THREEG << NET_RESTRICTION_LABEL_SHIFT)

#define ALL_NETWORK_RESTRICTIONS (CMM_LABEL_WIFI_ONLY | CMM_LABEL_THREEG_ONLY)


const char * net_type_name(int type);
bool has_network_restriction(u_long labels);
int network_fits_restriction(int type, u_long labels);

#include <string>
std::string describe_network_restrictions(u_long labels);

#endif /* LIBCMM_NET_RESTRICTION_H */
