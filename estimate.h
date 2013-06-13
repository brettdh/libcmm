#ifndef ESTIMATE_H_INCL_HUUA90Y8E4GEUGHA
#define ESTIMATE_H_INCL_HUUA90Y8E4GEUGHA

#include <sys/types.h>

u_long round_nearest(double val);

class Estimate {
  public:
    // pick estimate based on control limits
    // returns true on success, false if there are no observations yet
    bool get_estimate(u_long& est);
    bool get_estimate(double& est);
    
    void add_observation(double new_spot_value);

    void reset(double new_spot_value);
    
    Estimate();
  private:
    // keep as double for precision; convert to u_long on request
    double stable_estimate;
    double agile_estimate;
    double spot_value;
    double moving_range;
    double center_line;
    bool valid;
    
    bool spot_value_within_limits();
};

#endif
