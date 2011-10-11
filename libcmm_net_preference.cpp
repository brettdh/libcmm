#include "libcmm_net_preference.h"
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

bool has_network_preference(int labels)
{
    int pref_labels = labels & ALL_NETWORK_PREFS;
    return (pref_labels != 0 && pref_labels != ALL_NETWORK_PREFS);
}

int network_fits_preference(int type, int labels)
{
    int pref_labels = labels & ALL_NETWORK_PREFS;
    int pref_mask = type << NET_PREF_LABEL_SHIFT;
    return (pref_labels == 0 || pref_mask & pref_labels);
}

string describe_network_preferences(int labels)
{
    if (!has_network_preference(labels)) {
        return string("no preference");
    }

    ostringstream oss;
    if (labels & CMM_LABEL_WIFI_PREFERRED) {
        oss << "wifi";
    }
    if (labels & CMM_LABEL_THREEG_PREFERRED) {
        if (oss.str().length() > 0) {
            oss << "|";
        }
        oss << "3G";
    }
    return oss.str();
}
