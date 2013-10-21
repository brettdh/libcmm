#ifndef CONFIG_H_INCL_HGV0YDGSUOAH08FH93U2HBIRVSFB
#define CONFIG_H_INCL_HGV0YDGSUOAH08FH93U2HBIRVSFB

#include <string>
#include <map>

class Config {
  public:
    static Config *getInstance();
    
  protected:
    bool getBoolean(const std::string& key);
    std::string getString(const std::string& key);
    
    boolean hasDouble(const std::string& key);
    double getDouble(const std::string& key);
    
    Config();
    virtual void setup() {}
    virtual void validate() {}

 private:
    std::map<std::string, bool> boolean_options;
    std::map<std::string, std::string> string_options;
    std::map<std::string, double> double_options;

    void load();

    void readBooleanOption(const std::string& line, const std::string& key);
    void readStringOption(const std::string& line, const std::string& key);
    void readDoubleOption(const std::string& line, const std::string& key);

    static Config *instance;
};

#endif
