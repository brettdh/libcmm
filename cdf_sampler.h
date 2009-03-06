#ifndef cdf_sampler_h
#define cdf_sampler_h

#include <map>
#include <vector>
#include <string>

/* the pairs are (CDF(x), x) */
typedef std::map<double, double> cdf_map_t;

class CDFSampler {
  public:
    /* throws:
     *    Err if file can't be opened
     */
    CDFSampler(const char *filename, double duration);
    double sample();

  private:
    cdf_map_t cdf_map;
    std::vector<double> samples;
    size_t next;

    /* utility functions */
    void read_distribution(const char *filename);
    double sample_quantile(double alpha);
    void generate_samples();
};

class Err {
  public:
    std::string str;

    Err(std::string _str) : str(_str) {}
};


#endif /* include guard */
