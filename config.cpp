#include "config.h"
#include "debug.h"

#include <assert.h>

#include <string>
#include <fstream>
using std::string; using std::ifstream;

const char *CONFIG_FILE = "/etc/cmm_config";

// keys for boolean options
const char *DEBUG_OUTPUT_KEY = "debug";
const char *USE_BREADCRUMBS_ESTIMATES_KEY = "use_breadcrumbs_estimates";

// keys for string options
const char *ESTIMATOR_ERROR_SAVE_FILE_KEY = "save_estimator_errors";
const char *ESTIMATOR_ERROR_LOAD_FILE_KEY = "load_estimator_errors";


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
    ifstream config_input(CONFIG_FILE);
    if (config_input) {
        string line;
        while (getline(config_input, line)) {
            if (line.empty() || line[0] == '#') {
                continue;
            }
            
            readBooleanOption(line, DEBUG_OUTPUT_KEY);
            readBooleanOption(line, USE_BREADCRUMBS_ESTIMATES_KEY);
            readStringOption(line, ESTIMATOR_ERROR_SAVE_FILE_KEY);
            readStringOption(line, ESTIMATOR_ERROR_LOAD_FILE_KEY);
        }
        config_input.close();
    } else {
        dbgprintf_always("Warning: config file not read; couldn't open %s\n",
                         CONFIG_FILE);
    }
}

void
Config::readBooleanOption(const string& line, const string& key)
{
    if (has_param(line, key)) {
        dbgprintf_always("[config] %s=true\n", key.c_str());
        boolean_options[key] = true;
    }
}

void
Config::readStringOption(const string& line, const string& key)
{
    if (has_param(line, key)) {
        string value = get_param(line, key);
        
        dbgprintf_always("[config] %s=\"%s\"\n", key.c_str(), value.c_str());
        string_options[key] = value;
    }
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
