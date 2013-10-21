#include "intnw_config.h"
#include "debug.h"

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

// if set, only applies to prob-bounds.
static const string DISABLE_BANDWIDTH_ERROR_KEY = "disable_bandwidth_error";

static vector<string> my_boolean_option_keys = {
    DEBUG_OUTPUT_KEY,
    USE_BREADCRUMBS_ESTIMATES_KEY,
    RECORD_FAILOVER_LATENCY_KEY,
    PERIODIC_REEVALUATION_ENABLED_KEY,
    DISABLE_BANDWIDTH_ERROR_KEY,
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

static vector<string> my_unconstrained_string_option_keys = {
    ESTIMATOR_ERROR_SAVE_FILE_KEY,
    ESTIMATOR_ERROR_LOAD_FILE_KEY,
    NETWORK_STATS_SAVE_FILE_KEY,
    NETWORK_STATS_LOAD_FILE_KEY,
};

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

static vector<string> my_double_option_keys = {
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

static set<string> my_range_hints_option_keys = {
    WIFI_BANDWIDTH_RANGE_HINTS,
    WIFI_RTT_RANGE_HINTS,
    CELLULAR_BANDWIDTH_RANGE_HINTS,
    CELLULAR_RTT_RANGE_HINTS,
    WIFI_SESSION_DURATION_RANGE_HINTS,
    CELLULAR_SESSION_DURATION_RANGE_HINTS,
};

Config *Config::instance = nullptr;

Config *Config::getInstance()
{
    if (!instance) {
        instance = new Config;
        instance->loadConfig(CONFIG_FILE);
    }
    return instance;
}

void
Config::setup()
{
    for (const string& name : my_boolean_option_keys) {
        registerBooleanOption(name);
    }
    for (const string& name : my_double_option_keys) {
        registerDoubleOption(name);
    }
    for (const string& name : my_unconstrained_string_option_keys) {
        registerStringOption(name);
    }
    
    registerStringOption(ESTIMATOR_ERROR_EVAL_METHOD, get_method_name(TRUSTED_ORACLE), get_all_method_names());
    registerStringOption(INSTRUMENTS_DEBUG_LEVEL, "info",
                         {"none", "error", "info", "debug"});
    
    using namespace std::placeholders;
    registerOptionReader(bind(&Config::readRangeHintsOption, this, _1, _2), &my_range_hints_option_keys);
}

void
Config::validate()
{
    checkWifiSessionDistributionParamsValid();
    checkBayesianParamsValid();
    setInstrumentsDebugLevel();
}

void
Config::checkBayesianParamsValid()
{
    bool fail = false;
    if (getEstimatorErrorEvalMethod() == BAYESIAN) {
        for (const string& key : my_range_hints_option_keys) {
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
    if (hasString(INSTRUMENTS_DEBUG_LEVEL)) {
        instruments_debug_level_t level = instruments_debug_levels[getString(INSTRUMENTS_DEBUG_LEVEL)];
        instruments_set_debug_level(level);
    }
}

void
Config::checkWifiSessionDistributionParamsValid()
{
    bool has_shape = hasDouble(WEIBULL_WIFI_SESSION_LENGTH_DISTRIBUTION_SHAPE_KEY);
    bool has_scale = hasDouble(WEIBULL_WIFI_SESSION_LENGTH_DISTRIBUTION_SCALE_KEY);
    if (has_shape != has_scale) {
        dbgprintf_always("[config] error: need both shape and scale for weibull distribution\n");
        exit(EXIT_FAILURE);
    }
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

bool 
Config::getDisableBandwidthError()
{
    return getBoolean(DISABLE_BANDWIDTH_ERROR_KEY);
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
    if (hasDouble(WEIBULL_WIFI_SESSION_LENGTH_DISTRIBUTION_SHAPE_KEY) &&
        hasDouble(WEIBULL_WIFI_SESSION_LENGTH_DISTRIBUTION_SCALE_KEY)) {
        
        shape = getDouble(WEIBULL_WIFI_SESSION_LENGTH_DISTRIBUTION_SHAPE_KEY);
        scale = getDouble(WEIBULL_WIFI_SESSION_LENGTH_DISTRIBUTION_SCALE_KEY);
        return true;
    }
    return false;
}

double
Config::getWifiFailoverDelay()
{
    return getDouble(WIFI_FAILOVER_DELAY_KEY);
}
