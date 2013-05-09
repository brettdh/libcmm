#ifndef CONFIG_H_INCL_HGV0YDGSUOAH08FH93U2HBIRVSFB
#define CONFIG_H_INCL_HGV0YDGSUOAH08FH93U2HBIRVSFB

#include <string>
#include <map>

#include <instruments.h>
#include <instruments_private.h>

class Config {
  public:
    static Config *getInstance();
    
    bool getDebugOn();
    bool getUseBreadcrumbsEstimates();

    std::string getEstimatorErrorLoadFilename();
    std::string getEstimatorErrorSaveFilename();
    EvalMethod getEstimatorErrorEvalMethod();
  private:
    bool getBoolean(const std::string& key);
    std::string getString(const std::string& key);
    
    void readBooleanOption(const std::string& line, const std::string& key);
    void readStringOption(const std::string& line, const std::string& key);
    void readNumericalOption(const std::string& line, const std::string& key);
    
    std::map<std::string, bool> boolean_options;
    std::map<std::string, std::string> string_options;
    std::map<std::string, double> numerical_options;

    Config();
    void load();
    
    static Config *instance;
};

#endif
