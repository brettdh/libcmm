#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include "test_common.h"
#include "debug.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <libcmm.h>
#include <stdexcept>

#include <cppunit/Message.h>
#include <cppunit/Asserter.h>
#include <sstream>
#include <cmath>
using std::fabs;

#ifdef ANDROID
#include <android/log.h>
void LOG(const char *fmt, ...)
{
    return;
    va_list ap;
    va_start(ap, fmt);
    __android_log_vprint(ANDROID_LOG_INFO, "AndroidTestHarness", fmt, ap);
    va_end(ap);
}
#else
#define LOG dbgprintf_always
#endif

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
        int e = errno;
        perror(str);
        LOG("fatal error: %s: %s\n", str, strerror(e));
    }
}

void handle_error(bool condition, const char *msg)
{
    if (condition) {
        int e = errno;
        LOG("Exiting on fatal error: %s: %s\n", msg, strerror(e));
        exit(EXIT_FAILURE);
    }
}

void assertEqWithin(const std::string& actual_str, 
                    const std::string& expected_str, 
                    const std::string& message, 
                    double expected, double actual, double alpha,
                    CppUnit::SourceLine line)
{
    double val = fabs(expected - actual);
    double window = fabs(alpha * expected);
    
    const int precision = 15;
    char expected_buf[32];
    char actual_buf[32];
    char alpha_buf[32];
    char window_buf[32];
    char diff_buf[32];
    sprintf(expected_buf, "%.*g", precision, expected);
    sprintf(actual_buf, "%.*g", precision, actual);
    sprintf(alpha_buf, "%.*g", precision, alpha);
    sprintf(window_buf, "%.*g", precision, window);
    sprintf(diff_buf, "%.*g", precision, val);
    
    std::ostringstream expr;
    expr << "Expression: abs("
         << actual_str << " - " << expected_str << ") <= " << window_buf
         << "(" << expected_str << "*" << alpha_buf << ")";

    std::ostringstream values;
    values << "Values    : abs("
           << actual_buf << " - " << expected_buf << ") = " << diff_buf;

    CppUnit::Message msg("assertion failed", 
                         expr.str(), values.str(), message);
    CppUnit::Asserter::failIf(!(val <= window), msg, line);
}

int get_int_from_string(const char *str, const char *name)
{
    errno = 0;
    int val = strtol(str, NULL, 10);
    if (errno != 0) {
        std::ostringstream err;
        err << "Error: " << name << " argument must be an integer" << std::endl;
        throw std::runtime_error(err.str());
    }
    return val;
}

