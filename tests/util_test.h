#ifndef util_test_h_incl
#define util_test_h_incl

#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>
#include "common.h"

class UtilTest : public CppUnit::TestFixture {
    CPPUNIT_TEST_SUITE(UtilTest);
    CPPUNIT_TEST(testStringifyIP);
    CPPUNIT_TEST(testStringifyIPMultipleCalls);
    CPPUNIT_TEST_SUITE_END();    

  public:
    void testStringifyIP();
    void testStringifyIPMultipleCalls();
};

#endif
