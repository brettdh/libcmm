#ifndef estimation_test_h_incl
#define estimation_test_h_incl

#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>
#include "net_stats.h"
#include <string>

#include "flipflop_estimate.h"

class EstimationTest : public CppUnit::TestFixture {
    FlipFlopEstimate *estimate;
    QueuingDelay *delays;
    NetStats *stats;
    NetStats *other_stats;

    CPPUNIT_TEST_SUITE(EstimationTest);
    CPPUNIT_TEST(testFlipFlop);
    CPPUNIT_TEST(testQueuingDelay);
    CPPUNIT_TEST(testNetStatsSimple);
    CPPUNIT_TEST(testNetStatsWithQueuingDelay);
    //CPPUNIT_TEST(testNetStatsWithSenderDelay);
    CPPUNIT_TEST(testNetStatsSingleIROBQueuingDelay);
    CPPUNIT_TEST(testQueuingDelayInterleaved);
    CPPUNIT_TEST(testDisregardStripedIROBs);
    CPPUNIT_TEST(testFailoverDelay);
    CPPUNIT_TEST_SUITE_END();

  public:
    void setUp();
    void tearDown();

    void testFlipFlop();
    void testQueuingDelay();
    void testNetStatsSimple();
    void testNetStatsWithQueuingDelay();
    void testNetStatsWithSenderDelay();
    void testNetStatsSingleIROBQueuingDelay();
    void testQueuingDelayInterleaved();
    void testDisregardStripedIROBs();
    void testFailoverDelay();
  private:
    void assertStatsCorrect(u_long expected_bw, 
                            u_long expected_latency);
};

#endif
