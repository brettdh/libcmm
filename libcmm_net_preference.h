#ifndef LIBCMM_NET_PREFERENCE_H
#define LIBCMM_NET_PREFERENCE_H

#define NUM_NET_TYPES 2

#define NET_TYPE_WIFI 0x1
#define NET_TYPE_THREEG 0x2

#define NET_PREF_LABEL_SHIFT 16
#define CMM_LABEL_WIFI_PREFERRED (NET_TYPE_WIFI << NET_PREF_LABEL_SHIFT)
#define CMM_LABEL_THREEG_PREFERRED (NET_TYPE_THREEG << NET_PREF_LABEL_SHIFT)

#define ALL_NETWORK_PREFS (CMM_LABEL_WIFI_PREFERRED | CMM_LABEL_THREEG_PREFERRED)


const char * net_type_name(int type);
bool has_network_preference(int labels);
int network_fits_preference(int type, int labels);

#include <string>
std::string describe_network_preferences(int labels);

#endif /* LIBCMM_NET_PREFERENCE_H */
