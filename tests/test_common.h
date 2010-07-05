#ifndef test_common_h_incl
#define test_common_h_incl

#include <string>
#include <time.h>

void nowake_nanosleep(const struct timespec *duration);
void print_on_error(bool err, const char *str);

#include <cppunit/SourceLine.h>

void assertEqWithin(const std::string& actual_str, 
                    const std::string& expected_str, 
                    const std::string& message, 
                    double expected, double actual, double alpha,
                    CppUnit::SourceLine line);

#define MY_CPPUNIT_ASSERT_EQWITHIN_MESSAGE(expected,actual,alpha, message) \
    assertEqWithin(#actual, #expected, \
                   message, expected, actual, alpha, CPPUNIT_SOURCELINE())

void handle_error(bool condition, const char *msg);
int get_int_from_string(const char *str, const char *name, char *prog);

#endif
