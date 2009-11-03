#ifndef test_common_h_incl
#define test_common_h_incl

#include <time.h>

void nowake_nanosleep(const struct timespec *duration);
void print_on_error(bool err, const char *str);

#endif
