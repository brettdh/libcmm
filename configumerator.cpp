#include "intnw_config.h"
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

configumerator::Config::Config()
{
}

void configumerator::Config::loadConfig(const string& filename)
{
    setup();
    readFile(filename);
    validate();
}

void
configumerator::Config::registerOptionReader(std::function<void(const std::string&, const std::string&)> reader,
                                             decltype(boolean_option_keys) *keys)
{
    auto p = make_pair(reader, keys);
    readers.push_back(p);
}

void
configumerator::Config::registerBooleanOption(const string& name, bool default_value)
{
    boolean_option_keys.insert(name);
    boolean_options[name] = default_value;
}


void
configumerator::Config::registerStringOption(const string& name, const string& default_value, const set<string>& constraints)
{
    string_option_keys.insert(name);
    if (!constraints.empty()) {
        string_option_constraints[name] = constraints;
    }
    checkStringConstraints(name, default_value);
    string_options[name] = default_value;
}

void
configumerator::Config::registerDoubleOption(const string& name, double default_value)
{
    double_option_keys.insert(name);
    double_options[name] = default_value;
}

void
configumerator::Config::readFile(const string& filename)
{
    using namespace std::placeholders; // _1, _2, ..
    registerOptionReader(bind(&Config::readBooleanOption, this, _1, _2), &boolean_option_keys);
    registerOptionReader(bind(&Config::readStringOption, this, _1, _2), &string_option_keys);
    registerOptionReader(bind(&Config::readDoubleOption, this, _1, _2), &double_option_keys);
    
    ifstream config_input(filename.c_str());
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
                auto& keys = *pair.second;
                for (const string& key : keys) {
                    if (has_param(line, key)) {
                        reader(line, key);
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
                         filename.c_str());
        exit(EXIT_FAILURE);
    }
}


void
configumerator::Config::readBooleanOption(const string& line, const string& key)
{
    dbgprintf_always("[config] %s=true\n", key.c_str());
    boolean_options[key] = true;
}

void
configumerator::Config::checkStringConstraints(const string& key, const string& value)
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
configumerator::Config::readStringOption(const string& line, const string& key)
{
    string value = get_param(line, key);
    checkStringConstraints(key, value);
    dbgprintf_always("[config] %s=\"%s\"\n", key.c_str(), value.c_str());
    string_options[key] = value;
}

void
configumerator::Config::readDoubleOption(const string& line, const string& key)
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
configumerator::Config::getBoolean(const string& key)
{
    // false by default, if unset.
    return boolean_options[key];
}

bool 
configumerator::Config::hasString(const std::string& key)
{
    return string_options.count(key) > 0;
}

string
configumerator::Config::getString(const string& key)
{
    // empty by default, if unset.
    return string_options[key];
}

bool 
configumerator::Config::hasDouble(const std::string& key)
{
    return double_options.count(key) > 0;
}

double
configumerator::Config::getDouble(const string& key)
{
    return double_options[key];
}
