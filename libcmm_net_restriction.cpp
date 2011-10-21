#include "libcmm_net_restriction.h"
#include <string>
#include <sstream>

using std::string; using std::ostringstream;

static const char *net_type_strs[NUM_NET_TYPES + 1] = {"wifi", "3G", "unknown"};

const char *net_type_name(int type)
{
    int index = type - 1;
    if (index < 0 || index >= NUM_NET_TYPES) {
        index = NUM_NET_TYPES;
    }
    return net_type_strs[index];
}

bool has_network_restriction(u_long labels)
{
    int restriction_labels = labels & ALL_NETWORK_RESTRICTIONS;
    return (restriction_labels != 0 && restriction_labels != ALL_NETWORK_RESTRICTIONS);
}

int network_fits_restriction(int type, u_long labels)
{
    int restriction_labels = labels & ALL_NETWORK_RESTRICTIONS;
    int restriction_mask = type << NET_RESTRICTION_LABEL_SHIFT;
    return (restriction_labels == 0 || restriction_mask & restriction_labels);
}

string describe_network_restrictions(u_long labels)
{
    if (!has_network_restriction(labels)) {
        return string("no restriction");
    }

    ostringstream oss;
    if (labels & CMM_LABEL_WIFI_ONLY) {
        oss << "wifi";
    }
    if (labels & CMM_LABEL_THREEG_ONLY) {
        if (oss.str().length() > 0) {
            oss << "|";
        }
        oss << "3G";
    }
    return oss.str();
}
