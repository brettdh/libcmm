#include "intnw_config.h"

const char *CONFIG_FILE = "/etc/cmm_config";

// keys for boolean options
static const string DEBUG_OUTPUT_KEY = "debug";
static const string USE_BREADCRUMBS_ESTIMATES_KEY = "use_breadcrumbs_estimates";
static const string RECORD_FAILOVER_LATENCY_KEY = "record_failover_latency";
static const string PERIODIC_REEVALUATION_ENABLED_KEY = "periodic_reevaluation";

// if set, only applies to prob-bounds.
static const string DISABLE_BANDWIDTH_ERROR_KEY = "disable_bandwidth_error";

static vector<string> boolean_option_keys = {
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

static vector<string> unconstrained_string_option_keys = {
    ESTIMATOR_ERROR_SAVE_FILE_KEY,
    ESTIMATOR_ERROR_LOAD_FILE_KEY,
    NETWORK_STATS_SAVE_FILE_KEY,
    NETWORK_STATS_LOAD_FILE_KEY,
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

static Config& getInstance()
{
    if (!instance) {
        instance = configumerator::getInstance(factory);
    }
    return instance;
}

void
Config::setup()
{
    for (const string& name : boolean_option_keys) {
        registerBooleanOption(name);
    }

    
    registerStringOption(ESTIMATOR_ERROR_EVAL_METHOD, get_all_method_names());
    registerStringOption(INSTRUMENTS_DEBUG_LEVEL,
                         {"none", "error", "info", "debug"});


    string_options[ESTIMATOR_ERROR_EVAL_METHOD] = get_method_name(TRUSTED_ORACLE);
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
Config::checkWifiSessionDistributionParamsValid()
{
    if (double_options.count(WEIBULL_WIFI_SESSION_LENGTH_DISTRIBUTION_SHAPE_KEY) !=
        double_options.count(WEIBULL_WIFI_SESSION_LENGTH_DISTRIBUTION_SCALE_KEY)) {
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

double
Config::getWifiFailoverDelay()
{
    return double_options[WIFI_FAILOVER_DELAY_KEY];
}
