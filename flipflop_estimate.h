#ifndef ESTIMATE_H_INCL_HUUA90Y8E4GEUGHA
#define ESTIMATE_H_INCL_HUUA90Y8E4GEUGHA

#include <sys/types.h>
#include <iostream>
#include <string>
#include <vector>

u_long round_nearest(double val);

class FlipFlopEstimate {
  public:
    // pick estimate based on control limits
    // returns true on success, false if there are no observations yet
    bool get_estimate(u_long& est);
    bool get_estimate(double& est);
    
    void add_observation(double new_spot_value);

    void reset(double new_spot_value);

    void save(std::ostream& out);
    void load(std::istream& in);
    
    FlipFlopEstimate(const std::string& name_);
    FlipFlopEstimate(const FlipFlopEstimate& other);
    FlipFlopEstimate& operator=(const FlipFlopEstimate& other);
  private:
    std::string name;

    // keep as double for precision; convert to u_long on request
    double stable_estimate;
    double agile_estimate;
    double last_spot_value;
    double moving_range;
    double center_line;
    bool out_of_control;
    bool valid;

    void init();

    struct field {
        std::string name;
        double *dest;
    };
    std::vector<field> fields;
    
    bool spot_value_within_limits(double spot_value);
    double calc_limit_distance();

    void dbg_print(double new_spot_value);
};

#endif
