#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include "cdf_sampler.h"

int main(int argc, char *argv[])
{
    if (argc != 3) {
	fprintf(stderr, "Usage: %s <filename> <duration>\n", argv[0]);
	exit(1);
    }

    try {
	CDFSampler sampler(argv[1], atof(argv[2]));
	int i = 0;
	while (1) {
	    double sample = sampler.sample();
	    printf("Sample %d: %lf\n", ++i, sample);
	    if (sample < 0.0) {
		/* for our purposes, the CDF has no negative x values. */
		throw CDFErr("negative sample!  BUG IN CDF_SAMPLER CODE.");
	    }
	    sleep(1);
	}
    } catch (CDFErr &e) {
	fprintf(stderr, "Error: %s\n", e.str.c_str());
	exit(1);
    }
    
    return 0;
}
