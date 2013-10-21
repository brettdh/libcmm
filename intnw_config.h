#pragma once

#include "config.h"
#include <instruments.h>
#include <instruments_private.h>


class Config : public configumerator::Config {
  public:
    static ::Config *getInstance();

    bool getDebugOn();
    bool getUseBreadcrumbsEstimates();
    bool getRecordFailoverLatency();

    // if weibull_wifi_session_length_distribution_shape and
    //    weibull_wifi_session_length_distribution_scale are set,
    //    sets the parameters in shape and scale and returns true.
    // else returns false and leaves shape and scale unchanged.
    bool getWifiSessionLengthDistributionParams(double& shape, double& scale);

    bool getPeriodicReevaluationEnabled();
    bool getDisableBandwidthError();

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

  protected:
    virtual void setup();
    virtual void validate();

  private:
    std::map<std::string, EstimatorRangeHints> range_hints_options;

    void readRangeHintsOption(const std::string& line, const std::string& key);

    void checkBayesianParamsValid();
    void checkWifiSessionDistributionParamsValid();
    void setInstrumentsDebugLevel();

    static Config *instance;
};
