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
    bool getRecordFailoverLatency();

    // if weibull_wifi_session_length_distribution_shape and
    //    weibull_wifi_session_length_distribution_scale are set,
    //    sets the parameters in shape and scale and returns true.
    // else returns false and leaves shape and scale unchanged.
    bool getWifiSessionLengthDistributionParams(double& shape, double& scale);

    double getWifiFailoverDelay();

    std::string getNetworkStatsSaveFilename();
    std::string getNetworkStatsLoadFilename();

    std::string getEstimatorErrorLoadFilename();
    std::string getEstimatorErrorSaveFilename();
    EvalMethod getEstimatorErrorEvalMethod();
    
    EstimatorRangeHints getWifiBandwidthRangeHints();
    EstimatorRangeHints getWifiRttRangeHints();
    EstimatorRangeHints getCellularBandwidthRangeHints();
    EstimatorRangeHints getCellularRttRangeHints();
    EstimatorRangeHints getWifiSessionDurationRangeHints();
    EstimatorRangeHints getCellularSessionDurationRangeHints();
  private:
    bool getBoolean(const std::string& key);
    std::string getString(const std::string& key);
    
    void readBooleanOption(const std::string& line, const std::string& key);
    void readStringOption(const std::string& line, const std::string& key);
    void readDoubleOption(const std::string& line, const std::string& key);
    void readRangeHintsOption(const std::string& line, const std::string& key);

    void checkBayesianParamsValid();
    void checkWifiSessionDistributionParamsValid();
    void setInstrumentsDebugLevel();
    
    std::map<std::string, bool> boolean_options;
    std::map<std::string, std::string> string_options;
    std::map<std::string, double> double_options;
    std::map<std::string, EstimatorRangeHints> range_hints_options;

    Config();
    void load();
    
    static Config *instance;
};

#endif
