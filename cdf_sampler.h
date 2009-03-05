#ifndef cdf_sampler_h
#define cdf_sampler_h

#include <map>
#include <vector>

/* the pairs are (CDF(x), x) */
typedef std::map<double, double> cdf_map_t;

class CDFSampler {
  public:
    CDFSampler(const char *filename, double duration);
    double sample();
  private:
    cdf_map_t cdf_map;
    std::vector<double> prealloc_samples;
    int last_index;
};


#endif /* include guard */
