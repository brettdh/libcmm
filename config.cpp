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


static bool has_param(const string& line, const string& name)
{
    return line.compare(0, name.length(), name) == 0;
}

static string get_param(const string& line, const string& name)
{
    ASSERT(has_param(line, name));
    return line.substr(name.length() + 1);
}

Config::Config(string filename_)
    : filename(filename_)
{
}

void Config::loadConfig()
{
    setup();
    readFile();
    validate();
}

void
Config::registerOptionReader(decltype(&Config::readBooleanOption) reader, 
                             decltype(boolean_option_keys)& keys)
{
    readers[reader] = keys;
}

void
Config::registerBooleanOption(const string& name, bool default_value=false)
{
    boolean_option_keys.insert(name);
    boolean_options[name] = default_value);
}


void
Config::registerStringOption(const string& name, const string& default_value="", const set<string>& constraints = {})
{
    string_option_keys.insert(name);
    if (!constraints.empty()) {
        string_option_constraints[name] = constraints;
    }
    checkStringConstraints(name, default_value);
    string_options[name] = default_value
}

void
Config::registerDoubleOption(const string& name, double default_value=0.0)
{
    double_option_keys.insert(name);
    double_options[name] = default_value;
}

void
Config::load()
{
    registerOptionReader(&Config::readBooleanOption, boolean_option_keys);
    registerOptionReader(&Config::readStringOption, string_option_keys);
    registerOptionReader(&Config::readDoubleOption, double_option_keys);
    
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
            
            for (auto pair : readers) {
                auto reader = pair.first;
                auto& keys = pair.second;
                for (const string& key : keys) {
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
        dbgprintf_always("[config] Error: config file not read; couldn't open \"%s\"\n",
                         CONFIG_FILE);
        exit(EXIT_FAILURE);
    }
}


void
Config::readBooleanOption(const string& line, const string& key)
{
    dbgprintf_always("[config] %s=true\n", key.c_str());
    boolean_options[key] = true;
}

void
Config::checkStringConstraints(const string& key, const string& value)
{
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
}

void
Config::readStringOption(const string& line, const string& key)
{
    string value = get_param(line, key);
    check_string_constraints(key, value);
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
