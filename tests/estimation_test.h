#ifndef estimation_test_h_incl
#define estimation_test_h_incl

#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>
#include "net_stats.h"

class EstimationTest : public CppUnit::TestFixture {
    Estimate *estimate;
    QueuingDelay *delays;

    CPPUNIT_TEST_SUITE(EstimationTest);
    CPPUNIT_TEST(testFlipFlop);
    CPPUNIT_TEST(testQueuingDelay);
    CPPUNIT_TEST_SUITE_END();

  public:
    void setUp();
    void tearDown();

    void testFlipFlop();
    void testQueuingDelay();
};

#endif
