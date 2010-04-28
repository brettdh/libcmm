#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include "test_common.h"

void nowake_nanosleep(const struct timespec *duration)
{
    struct timespec time_left;
    struct timespec sleep_time = *duration;
    while (nanosleep(&sleep_time, &time_left) < 0) {
        if (errno != EINTR) {
            perror("nanosleep");
            exit(-1);
        }
        sleep_time = time_left;
    }
}

void print_on_error(bool err, const char *str)
{
    if (err) {
        perror(str);
    }
}

// void assertLessEq(std::string msg, T expected, T actual,
//                   CppUnit::SourceLine line)
// {
//     CppUnit::Asserter::failIf(!(actual > expected),
//                               CPPUNIT_NS::Message("assertion failed",
//                                                   "Expression: "
//                                                   #condition,
//                                                   message),
//                               line);
// }
