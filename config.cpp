#include "config.h"
#include "debug.h"

#include <assert.h>

#include <map>
#include <set>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <algorithm>
using std::string; using std::ifstream;
using std::map; using std::set; using std::vector;
using std::istringstream; using std::ostringstream;
using std::for_each;

const char *CONFIG_FILE = "/etc/cmm_config";

// keys for boolean options
static const string DEBUG_OUTPUT_KEY = "debug";
static const string USE_BREADCRUMBS_ESTIMATES_KEY = "use_breadcrumbs_estimates";

static vector<string> boolean_option_keys = {
    DEBUG_OUTPUT_KEY,
    USE_BREADCRUMBS_ESTIMATES_KEY
};

// keys for string options
static const string ESTIMATOR_ERROR_SAVE_FILE_KEY = "save_estimator_errors";
static const string ESTIMATOR_ERROR_LOAD_FILE_KEY = "load_estimator_errors";
static const string ESTIMATOR_ERROR_EVAL_METHOD   = "estimator_error_eval_method";

static vector<string> string_option_keys = {
    ESTIMATOR_ERROR_SAVE_FILE_KEY,
    ESTIMATOR_ERROR_LOAD_FILE_KEY,
    ESTIMATOR_ERROR_EVAL_METHOD
};

static map<string, set<string> > string_option_constraints = {
    { ESTIMATOR_ERROR_EVAL_METHOD, { "empirical_error", "probabalistic", "bayesian" } }
};

// keys for numerical values
static const string WIFI_BANDWIDTH_RANGE_HINTS     = "wifi_bandwidth_range_hints";
static const string WIFI_RTT_RANGE_HINTS           = "wifi_rtt_range_hints";
static const string CELLULAR_BANDWIDTH_RANGE_HINTS = "cellular_bandwidth_range_hints";
static const string CELLULAR_RTT_RANGE_HINTS       = "cellular_rtt_range_hints";

static vector<string> numerical_option_keys = {
    WIFI_BANDWIDTH_RANGE_HINTS,
    WIFI_RTT_RANGE_HINTS,
    CELLULAR_BANDWIDTH_RANGE_HINTS,
    CELLULAR_RTT_RANGE_HINTS
};


static bool has_param(const string& line, const string& name)
{
    return line.find(name) != string::npos;
}

static string get_param(const string& line, const string& name)
{
    assert(has_param(line, name));
    return line.substr(name.length() + 1);
}

Config *Config::instance = NULL;

Config*
Config::getInstance()
{
    if (!instance) {
        instance = new Config;
    }
    return instance;
}

Config::Config()
{
    load();
}

void
Config::load()
{
    vector<decltype(&Config::readBooleanOption)> readers = {
        &Config::readBooleanOption, 
        &Config::readStringOption, 
        &Config::readNumericalOption 
    };
    vector<decltype(boolean_option_keys)> keys = { 
        boolean_option_keys, 
        string_option_keys, 
        numerical_option_keys
    };

    ifstream config_input(CONFIG_FILE);
    if (config_input) {
        string line;
        size_t line_num = 0;
        while (getline(config_input, line)) {
            ++line_num;
            if (line.empty() || line[0] == '#') {
                continue;
            }
            bool matched = false;
            
            for (size_t i = 0; i < readers.size(); ++i) {
                auto& reader = readers[i];
                for (const string& key : keys[i]) {
                    if (has_param(line, key)) {
                        (this->*reader)(line, key);
                        matched = true;
                    }
                }
            }
            if (!matched) {
                dbgprintf_always("[config] unrecognized config param on line %zu: \"%s\"\n",
                                 line_num, line.c_str());
                exit(EXIT_FAILURE);
            }
        }
        config_input.close();
    } else {
        dbgprintf_always("Warning: config file not read; couldn't open \"%s\"\n",
                         CONFIG_FILE);
    }
}

void
Config::readBooleanOption(const string& line, const string& key)
{
    dbgprintf_always("[config] %s=true\n", key.c_str());
    boolean_options[key] = true;
}

void
Config::readStringOption(const string& line, const string& key)
{
    string value = get_param(line, key);
        
    if (string_option_constraints.count(key) > 0) {
        auto& constraints = string_option_constraints[key];
        if (constraints.count(value) == 0) {
            dbgprintf_always("[config] invalid value \"%s\" for option \"%s\"\n", value.c_str(), key.c_str());
            ostringstream oss;
            for_each(constraints.begin(), constraints.end(),
                     [&](const string& s) {
                         oss << s << " ";
                     });
            dbgprintf_always("[config] valid types are { %s}\n", oss.str().c_str());
            exit(EXIT_FAILURE);
        }
    }
    dbgprintf_always("[config] %s=\"%s\"\n", key.c_str(), value.c_str());
    string_options[key] = value;
}

void
Config::readNumericalOption(const string& line, const string& key)
{
    string dummy;
    double value;
    istringstream iss(line);
    if (!(iss >> dummy >> value)) {
        dbgprintf_always("[config] failed to parse number from line: \"%s\"\n", line.c_str());
        exit(EXIT_FAILURE);
    }
        
    size_t precision = line.length() - dummy.length();
    dbgprintf_always("[config] %s=%.*f\n", key.c_str(), precision, value);
    numerical_options[key] = value;
}


bool
Config::getBoolean(const string& key)
{
    // false by default, if unset.
    return boolean_options[key];
}

string
Config::getString(const string& key)
{
    // empty by default, if unset.
    return string_options[key];
}

bool
Config::getDebugOn()
{
    return getBoolean(DEBUG_OUTPUT_KEY);
}

bool
Config::getUseBreadcrumbsEstimates()
{
    return getBoolean(USE_BREADCRUMBS_ESTIMATES_KEY);
}

string
Config::getEstimatorErrorSaveFilename()
{
    return getString(ESTIMATOR_ERROR_SAVE_FILE_KEY);
}

string
Config::getEstimatorErrorLoadFilename()
{
    return getString(ESTIMATOR_ERROR_LOAD_FILE_KEY);
}
