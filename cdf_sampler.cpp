#include "cdf_sampler.h"

void read_availability_distribution(char *cdf_filename, cdf_map_t &cdf_map)
{
    cdf_map.clear();
    /* read file */
    /* ... */
    /* yay, done reading the file */
    
    /* fake it for now; uniform distribution */
    for (int i = 0; i < 10; i++) {
        cdf_map[((double)i/10.0)] = (double)i/10.0;
    }
}

/* sample from the quantile function (inverse CDF) */
double sample_quantile(const cdf_map_t &cdf_map, double alpha)
{
    cdf_map_t::const_iterator lower = cdf_map.lower_bound(alpha);
    if (alpha == lower->first) 
        return lower->second;

    cdf_map_t::const_iterator upper = cdf_map.upper_bound(alpha);
    if (alpha == upper->first) 
        return upper->second;

    /* interpolate on the CDF.  For large data sets, 
     * should be close to accurate. */
    double slope = (upper->second - lower->second)/(upper->first - lower->first);
    double delta = alpha - lower->first;
    double estimate = (lower->second + (slope*delta));
    return estimate;
}

void prepare_scout(, double total_duration)
{

}

