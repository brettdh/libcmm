#include "cdf_sampler.h"
#include <cstdlib>
#include <cstdio>
#include <string>

using std::string;

/* returns a pseudo-random double in the range [0,1]. */
static double randDouble() {
    return (random() / (static_cast<double>(RAND_MAX) + 1.0));
}

CDFSampler::CDFSampler(const char *cdf_filename, double duration)
{
    read_distribution(cdf_filename);
    next = 0;
    if (duration > 0) {
	/* pre-sample the given duration so we don't have to do
	 * potentiall large binary searches on the fly */
	double total = 0.0;
	while (total < duration) {
	    double sample = sample_quantile();
	    samples.push_back(sample);
	    total += sample;
	}
    }
}

double
CDFSampler::sample()
{
    if (next < samples.size()) {
	return samples[next++];
    } else {
	return sample_quantile();
    }
}

void 
CDFSampler::read_distribution(const char *cdf_filename)
{
    double val, percent = -1.0;
    cdf_map.clear();
    FILE *input = fopen(cdf_filename, "r");
    if (!input) {
	throw CDFErr("Failed to open file");
    }

    while (fscanf(input, "%lf %lf", &val, &percent) == 2) {
	if (percent < 0.0) {
	    fclose(input);
	    throw CDFErr("Invalid data in file");
	}
	
	if (cdf_map.find(percent) != cdf_map.end()) {
	    fclose(input);
	    fprintf(stderr, "Duplicate value %f found in file\n", percent);
	    throw CDFErr("CDF function is not invertible");
	}
	cdf_map[percent] = val;
    }
    fclose(input);
    if (percent < 0.0) {
	throw CDFErr("No data read from file");
    }
}

/* sample from the quantile function (inverse CDF) */
double 
CDFSampler::sample_quantile()
{
    double alpha = randDouble();
    double upper_percent, lower_percent, upper_val, lower_val;
    cdf_map_t::const_iterator upper = cdf_map.upper_bound(alpha);
    upper_percent = upper->first;
    upper_val = upper->second;

    if (upper == cdf_map.begin()) {
	/* try again.  Probably the next sample won't fall outside
	 * the range of the CDF. */
	return sample_quantile();
    }
    
    cdf_map_t::const_reverse_iterator lower(upper);
    lower++;

    if (lower == cdf_map.rend()) {
	/* try again.  Probably the next sample won't fall outside
	 * the range of the CDF. */
	return sample_quantile();
    }

    //cdf_map_t::const_iterator lower = cdf_map.lower_bound(alpha);
    lower_percent = lower->first;
    lower_val = lower->second;

    //printf("alpha: %lf\n", alpha);
    //printf("upper: %%=%lf val=%lf\n", upper_percent, upper_val);
    //printf("lower: %%=%lf val=%lf\n", lower_percent, lower_val);

    if (alpha == lower_percent) 
        return lower_val;

    if (alpha == upper_percent) 
        return upper_val;

    /* interpolate on the CDF.  For large data sets, 
     * should be close to accurate. */
    double slope = ((upper_val - lower_val)
                    / (upper_percent - lower_percent));
    double delta = alpha - lower_percent;
    double estimate = (lower_val + (slope*delta));

    //printf("percents: lower %lf upper %lf\n", lower_percent, upper_percent);
    //printf("vals: lower %lf upper %lf\n", lower_val, upper_val);
    //printf("alpha: %lf  delta: %lf\n", alpha, delta);
    return estimate;
}
