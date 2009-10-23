#ifndef estimation_test_h_incl
#define estimation_test_h_incl

#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>
#include "net_stats.h"

class EstimationTest : public CppUnit::TestFixture {
    Estimate *estimate;
    QueuingDelay *delays;
    NetStats *stats;

    CPPUNIT_TEST_SUITE(EstimationTest);
    CPPUNIT_TEST(testFlipFlop);
    CPPUNIT_TEST(testQueuingDelay);
    CPPUNIT_TEST(testNetStatsSimple);
    CPPUNIT_TEST(testNetStatsWithQueuingDelay);
    CPPUNIT_TEST(testNetStatsWithSenderDelay);
    CPPUNIT_TEST_SUITE_END();

  public:
    void setUp();
    void tearDown();

    void testFlipFlop();
    void testQueuingDelay();
    void testNetStatsSimple();
    void testNetStatsWithQueuingDelay();
    void testNetStatsWithSenderDelay();

  private:
    void assertStatsCorrect(u_long expected_bw, 
                            u_long expected_latency);
};

#endif
