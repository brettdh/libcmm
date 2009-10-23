#include <cppunit/Test.h>
#include <cppunit/TestAssert.h>
#include <cppunit/extensions/HelperMacros.h>
#include "estimation_test.h"
#include "timeops.h"
#include "test_common.h"
#include <netinet/in.h>
#include <sys/types.h>

CPPUNIT_TEST_SUITE_REGISTRATION(EstimationTest);

void
EstimationTest::setUp()
{
    estimate = new Estimate;
    delays = new QueuingDelay;

    struct in_addr dummy = {0};
    stats = new NetStats(dummy, dummy);
}

void
EstimationTest::tearDown()
{
    delete estimate;
    delete delays;
    delete stats;
}

#define STABLE_GAIN 0.9
#define AGILE_GAIN 0.1

void
EstimationTest::testFlipFlop()
{
    double agile_estimate = 0.0;
    double stable_estimate = 0.0;

    double spot_value = 0.0;
    for (int i = 0; i < 10; ++i) {
        update_EWMA(stable_estimate, spot_value, STABLE_GAIN);
        update_EWMA(agile_estimate, spot_value, AGILE_GAIN);
        estimate->add_observation(spot_value);
    }

    u_long est_value;
    bool ret = estimate->get_estimate(est_value);
    CPPUNIT_ASSERT(ret);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("Returns agile estimate at first",
                                 (u_long)agile_estimate, 
                                 est_value);

    spot_value = 2000.0;
    update_EWMA(stable_estimate, spot_value, STABLE_GAIN);
    update_EWMA(agile_estimate, spot_value, AGILE_GAIN);
    estimate->add_observation(spot_value);

    ret = estimate->get_estimate(est_value);
    CPPUNIT_ASSERT(ret);    
    printf("stable: %lu agile: %lu flipflop: %lu\n",
           (u_long)stable_estimate, (u_long)agile_estimate,
           est_value);

    CPPUNIT_ASSERT_EQUAL_MESSAGE("Returns stable estimate "
                                 "when variance is too big",
                                 (u_long)stable_estimate, 
                                 est_value);

    for (int i = 0; i < 20; ++i) {
        update_EWMA(stable_estimate, spot_value, STABLE_GAIN);
        update_EWMA(agile_estimate, spot_value, AGILE_GAIN);
        estimate->add_observation(spot_value);

        ret = estimate->get_estimate(est_value);
        CPPUNIT_ASSERT(ret);    
        printf("stable: %lu agile: %lu flipflop: %lu\n",
               (u_long)stable_estimate, (u_long)agile_estimate,
               est_value);
    }
}

void
EstimationTest::testQueuingDelay()
{
    struct timeval reference = {0,0};
    struct timeval tv;
    tv = delays->add_message(3, 1);
    printf("Message 1: queuing delay = %lu.%06lu\n",
           tv.tv_sec, tv.tv_usec);
    CPPUNIT_ASSERT_MESSAGE("First message - no queuing delay",
                           timercmp(&reference, &tv, ==));

    sleep(2);
    tv = delays->add_message(3, 3);
    printf("Message 2: queuing delay = %lu.%06lu\n",
           tv.tv_sec, tv.tv_usec);
    CPPUNIT_ASSERT_MESSAGE("First message - nonzero delay",
                           timercmp(&reference, &tv, <));    
    reference.tv_sec = 1;
    CPPUNIT_ASSERT_MESSAGE("First message - less than 1 delay",
                           timercmp(&reference, &tv, >));
}

void
EstimationTest::testNetStats()
{
    struct timeval zero = {0,0};
    struct timespec sleeptime = {1, 40000000};

    stats->report_send_event(1, 5);
    nowake_nanosleep(&sleeptime);
    stats->report_ack(1, zero);
    
    sleeptime.tv_sec = 2;
    stats->report_send_event(2, 10);
    nowake_nanosleep(&sleeptime);
    stats->report_ack(2, zero);
    
    u_long bw = 0, latency = 0;
    bool ret = stats->get_estimate(NET_STATS_BW_UP, bw);
    ret = ret && stats->get_estimate(NET_STATS_LATENCY, latency);
    CPPUNIT_ASSERT(ret);

    u_long expected_bw = 5;
    u_long expected_latency = 20;
    CPPUNIT_ASSERT_EQUAL_MESSAGE("Bandwidth estimate matches expected",
                                 expected_bw, bw);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("Latency estimate matches expected",
                                 expected_latency, latency);
}
