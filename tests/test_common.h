#ifndef test_common_h_incl
#define test_common_h_incl

#include <string>
#include <time.h>

void nowake_nanosleep(const struct timespec *duration);
void print_on_error(bool err, const char *str);

// namespace CppUnit {
//     class SourceLine;
// }
// 
// template <typename T>
// void assertLessEq(std::string msg, T expected, T actual,
//                   CppUnit::SourceLine line);
// 
// #define MY_CPPUNIT_ASSERT_LESSEQ_MESSAGE(message,expected,actual) \
//     assertLessEq(message, expected, actual, CPPUNIT_SOURCELINE())

#endif
