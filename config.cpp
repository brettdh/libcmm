#include "config.h"
#include "debug.h"

#include <instruments.h>    
#include <instruments_private.h>

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
static const string RECORD_FAILOVER_LATENCY_KEY = "record_failover_latency";
static const string PERIODIC_REEVALUATION_ENABLED_KEY = "periodic_reevaluation";

static vector<string> boolean_option_keys = {
    DEBUG_OUTPUT_KEY,
    USE_BREADCRUMBS_ESTIMATES_KEY,
    RECORD_FAILOVER_LATENCY_KEY,
    PERIODIC_REEVALUATION_ENABLED_KEY,
};

// keys for string options
static const string ESTIMATOR_ERROR_SAVE_FILE_KEY = "save_estimator_errors";
static const string ESTIMATOR_ERROR_LOAD_FILE_KEY = "load_estimator_errors";

// currently this file is only used to save/load wifi session duration;
//  the breadcrumbs db is used for the rest.
static const string NETWORK_STATS_SAVE_FILE_KEY = "save_network_stats";
static const string NETWORK_STATS_LOAD_FILE_KEY = "load_network_stats";

static const string ESTIMATOR_ERROR_EVAL_METHOD   = "estimator_error_eval_method";
static const string INSTRUMENTS_DEBUG_LEVEL   = "instruments_debug_level";

static vector<string> string_option_keys = {
    ESTIMATOR_ERROR_SAVE_FILE_KEY,
    ESTIMATOR_ERROR_LOAD_FILE_KEY,
    NETWORK_STATS_SAVE_FILE_KEY,
    NETWORK_STATS_LOAD_FILE_KEY,
    ESTIMATOR_ERROR_EVAL_METHOD,
    INSTRUMENTS_DEBUG_LEVEL
};

static map<string, set<string> > string_option_constraints;

static map<string, instruments_debug_level_t> instruments_debug_levels = {
    {"none", INSTRUMENTS_DEBUG_LEVEL_NONE},
    {"error", INSTRUMENTS_DEBUG_LEVEL_ERROR},
    {"info", INSTRUMENTS_DEBUG_LEVEL_INFO},
    {"debug", INSTRUMENTS_DEBUG_LEVEL_DEBUG},
};

static const string WEIBULL_WIFI_SESSION_LENGTH_DISTRIBUTION_SHAPE_KEY = 
    "weibull_wifi_session_length_distribution_shape";
static const string WEIBULL_WIFI_SESSION_LENGTH_DISTRIBUTION_SCALE_KEY = 
    "weibull_wifi_session_length_distribution_scale";
static const string WIFI_FAILOVER_DELAY_KEY = "wifi_failover_delay";

static vector<string> double_option_keys = {
    WEIBULL_WIFI_SESSION_LENGTH_DISTRIBUTION_SHAPE_KEY,
    WEIBULL_WIFI_SESSION_LENGTH_DISTRIBUTION_SCALE_KEY, 
    WIFI_FAILOVER_DELAY_KEY
};


// keys for range_hints values
static const string WIFI_BANDWIDTH_RANGE_HINTS            = "wifi_bandwidth_range_hints";
static const string WIFI_RTT_RANGE_HINTS                  = "wifi_rtt_range_hints";
static const string CELLULAR_BANDWIDTH_RANGE_HINTS        = "cellular_bandwidth_range_hints";
static const string CELLULAR_RTT_RANGE_HINTS              = "cellular_rtt_range_hints";
static const string WIFI_SESSION_DURATION_RANGE_HINTS     = "wifi_session_duration_range_hints";
static const string CELLULAR_SESSION_DURATION_RANGE_HINTS = "cellular_session_duration_range_hints";

static vector<string> range_hints_option_keys = {
    WIFI_BANDWIDTH_RANGE_HINTS,
    WIFI_RTT_RANGE_HINTS,
    CELLULAR_BANDWIDTH_RANGE_HINTS,
    CELLULAR_RTT_RANGE_HINTS,
    WIFI_SESSION_DURATION_RANGE_HINTS,
    CELLULAR_SESSION_DURATION_RANGE_HINTS,
};

static bool has_param(const string& line, const string& name)
{
    return line.compare(0, name.length(), name) == 0;
}

static string get_param(const string& line, const string& name)
{
    ASSERT(has_param(line, name));
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
    string_option_constraints[ESTIMATOR_ERROR_EVAL_METHOD] = get_all_method_names();
    string_option_constraints[INSTRUMENTS_DEBUG_LEVEL] = {
        "none", "error", "info", "debug"
    };

    string_options[ESTIMATOR_ERROR_EVAL_METHOD] = get_method_name(TRUSTED_ORACLE);

    vector<decltype(&Config::readBooleanOption)> readers = {
        &Config::readBooleanOption, 
        &Config::readStringOption, 
        &Config::readRangeHintsOption,
        &Config::readDoubleOption
    };
    vector<decltype(boolean_option_keys)> keys = {
        boolean_option_keys, 
        string_option_keys, 
        range_hints_option_keys,
        double_option_keys
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

        checkWifiSessionDistributionParamsValid();
        checkBayesianParamsValid();
        setInstrumentsDebugLevel();
    } else {
        dbgprintf_always("[config] Error: config file not read; couldn't open \"%s\"\n",
                         CONFIG_FILE);
        exit(EXIT_FAILURE);
    }
}

void
Config::checkBayesianParamsValid()
{
    bool fail = false;
    if (getEstimatorErrorEvalMethod() == BAYESIAN) {
        for (const string& key : range_hints_option_keys) {
            if (range_hints_options.count(key) == 0) {
                dbgprintf_always("[config] Error: bayesian method requires %s option\n", key.c_str());
                fail = true;
            }
        }
    }
    if (fail) {
        exit(EXIT_FAILURE);
    }
}

void
Config::setInstrumentsDebugLevel()
{
    if (string_options.count(INSTRUMENTS_DEBUG_LEVEL) > 0) {
        instruments_debug_level_t level = instruments_debug_levels[getString(INSTRUMENTS_DEBUG_LEVEL)];
        instruments_set_debug_level(level);
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
        if (find(constraints.begin(), constraints.end(), value) == constraints.end()) {
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
Config::readRangeHintsOption(const string& line, const string& key)
{
    string dummy;
    istringstream iss(line);
    EstimatorRangeHints hints;
    if (!(iss >> dummy >> hints.min >> hints.max >> hints.num_bins)) {
        dbgprintf_always("[config] failed to parse number from line: \"%s\"\n", line.c_str());
        exit(EXIT_FAILURE);
    }

    vector<size_t> precision;
    istringstream precision_iss(line);
    precision_iss >> dummy;
    for (size_t i = 0; i < 2; ++i) {
        string value;
        precision_iss >> value;
        ASSERT(precision_iss);
        precision.push_back(value.length());
    }

    dbgprintf_always("[config] %s=[min=%.*f, max=%.*f, num_bins=%zu]\n", 
                     key.c_str(), 
                     precision[0], hints.min,
                     precision[1], hints.max,
                     hints.num_bins);
    range_hints_options[key] = hints;
}

void
Config::readDoubleOption(const string& line, const string& key)
{
    string value_str = get_param(line, key);

    string dummy;
    istringstream iss(line);
    double value;
    if (!(iss >> dummy >> value)) {
        dbgprintf_always("[config] failed to parse number from line: \"%s\"\n", line.c_str());
        exit(EXIT_FAILURE);
    }
    dbgprintf_always("[config] %s=%.*f\n", key.c_str(), value_str.length(), value);
    double_options[key] = value;
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

bool
Config::getRecordFailoverLatency()
{
    return getBoolean(RECORD_FAILOVER_LATENCY_KEY);
}

bool 
Config::getPeriodicReevaluationEnabled()
{
    return getBoolean(PERIODIC_REEVALUATION_ENABLED_KEY);
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

string
Config::getNetworkStatsSaveFilename()
{
    return getString(NETWORK_STATS_SAVE_FILE_KEY);
}

string
Config::getNetworkStatsLoadFilename()
{
    return getString(NETWORK_STATS_LOAD_FILE_KEY);
}

EvalMethod
Config::getEstimatorErrorEvalMethod()
{
    string name = getString(ESTIMATOR_ERROR_EVAL_METHOD);
    return get_method(name.c_str());
}

EstimatorRangeHints
Config:: getWifiBandwidthRangeHints()
{
    return range_hints_options[WIFI_BANDWIDTH_RANGE_HINTS];
}

EstimatorRangeHints
Config:: getWifiRttRangeHints()
{
    return range_hints_options[WIFI_RTT_RANGE_HINTS];
}

EstimatorRangeHints
Config:: getCellularBandwidthRangeHints()
{
    return range_hints_options[CELLULAR_BANDWIDTH_RANGE_HINTS];
}

EstimatorRangeHints
Config:: getCellularRttRangeHints()
{
    return range_hints_options[CELLULAR_RTT_RANGE_HINTS];
}

EstimatorRangeHints
Config::getWifiSessionDurationRangeHints()
{
    return range_hints_options[WIFI_SESSION_DURATION_RANGE_HINTS];
}

EstimatorRangeHints
Config::getCellularSessionDurationRangeHints()
{
    return range_hints_options[CELLULAR_SESSION_DURATION_RANGE_HINTS];
}

bool 
Config::getWifiSessionLengthDistributionParams(double& shape, double& scale)
{
    if (double_options.count(WEIBULL_WIFI_SESSION_LENGTH_DISTRIBUTION_SHAPE_KEY) > 0 &&
        double_options.count(WEIBULL_WIFI_SESSION_LENGTH_DISTRIBUTION_SCALE_KEY) > 0) {
        
        shape = double_options[WEIBULL_WIFI_SESSION_LENGTH_DISTRIBUTION_SHAPE_KEY];
        scale = double_options[WEIBULL_WIFI_SESSION_LENGTH_DISTRIBUTION_SCALE_KEY];
        return true;
    }
    return false;
}

void
Config::checkWifiSessionDistributionParamsValid()
{
    if (double_options.count(WEIBULL_WIFI_SESSION_LENGTH_DISTRIBUTION_SHAPE_KEY) !=
        double_options.count(WEIBULL_WIFI_SESSION_LENGTH_DISTRIBUTION_SCALE_KEY)) {
        dbgprintf_always("[config] error: need both shape and scale for weibull distribution\n");
        exit(EXIT_FAILURE);
    }
}

double
Config::getWifiFailoverDelay()
{
    return double_options[WIFI_FAILOVER_DELAY_KEY];
}
