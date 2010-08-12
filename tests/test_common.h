#ifndef TEST_COMMON_H_INCLUDED
#define TEST_COMMON_H_INCLUDED

#include <string>
#include <time.h>

#ifndef handle_error
void handle_error(bool condition, const char *msg);
#endif

void nowake_nanosleep(const struct timespec *duration);
void print_on_error(bool err, const char *str);
void abort_test(const char *msg);
int get_int_from_string(const char *str, const char *name);

#include <cppunit/SourceLine.h>

void assertEqWithin(const std::string& actual_str, 
                    const std::string& expected_str, 
                    const std::string& message, 
                    double expected, double actual, double alpha,
                    CppUnit::SourceLine line);

#define MY_CPPUNIT_ASSERT_EQWITHIN_MESSAGE(expected,actual,alpha, message) \
  assertEqWithin(#actual, #expected,                                    \
                 message, expected, actual, alpha, CPPUNIT_SOURCELINE())


#ifdef ANDROID
void DEBUG_LOG(const char *fmt, ...)
    __attribute__((format(printf, 1, 2)));
#else
#include "debug.h"
#define DEBUG_LOG dbgprintf_always
#endif

#endif
