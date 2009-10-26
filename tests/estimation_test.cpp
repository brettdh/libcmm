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

static u_long round_nearest(double val)
{
    return static_cast<u_long>(val + 0.5);
}

void
EstimationTest::testFlipFlop()
{
    double agile_estimate = 0.0;
    double stable_estimate = 0.0;

    double spot_value = 0.0;
    for (int i = 0; i < 10; ++i) {
        update_EWMA(stable_estimate, spot_value, STABLE_GAIN);
        update_EWMA(agile_estimate, spot_value, AGILE_GAIN);
        estimate->add_observation((u_long)spot_value);
    }

    u_long est_value;
    bool ret = estimate->get_estimate(est_value);
    CPPUNIT_ASSERT(ret);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("Returns agile estimate at first",
                                 round_nearest(agile_estimate),
                                 est_value);

    spot_value = 2000.0;
    update_EWMA(stable_estimate, spot_value, STABLE_GAIN);
    update_EWMA(agile_estimate, spot_value, AGILE_GAIN);
    estimate->add_observation((u_long)spot_value);

    ret = estimate->get_estimate(est_value);
    CPPUNIT_ASSERT(ret);    
    printf("stable: %lu agile: %lu flipflop: %lu\n",
           round_nearest(stable_estimate), 
           round_nearest(agile_estimate),
           est_value);

    CPPUNIT_ASSERT_EQUAL_MESSAGE("Returns stable estimate "
                                 "when variance is too big",
                                 round_nearest(stable_estimate), 
                                 est_value);

    for (int i = 0; i < 20; ++i) {
        update_EWMA(stable_estimate, spot_value, STABLE_GAIN);
        update_EWMA(agile_estimate, spot_value, AGILE_GAIN);
        estimate->add_observation((u_long)spot_value);

        ret = estimate->get_estimate(est_value);
        CPPUNIT_ASSERT(ret);    
        printf("stable: %lu agile: %lu flipflop: %lu\n",
               round_nearest(stable_estimate),
               round_nearest(agile_estimate),
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
EstimationTest::testNetStatsSimple()
{
    // true bw: 5000 Bps  true latency: 20ms

    struct timeval zero = {0,0};

    // bump up bandwidth estimate to avoid rounding error
    struct timespec sleeptime = {1, 40 * 1000 * 1000};

    stats->report_send_event(1, 5000);
    nowake_nanosleep(&sleeptime);
    stats->report_ack(1, zero);
    
    sleeptime.tv_sec = 2;
    stats->report_send_event(2, 10000);
    nowake_nanosleep(&sleeptime);
    stats->report_ack(2, zero);
    
    assertStatsCorrect(5000, 20);
}

void 
EstimationTest::testNetStatsWithQueuingDelay()
{
    // true bw: 5000 Bps  true latency: 20ms
    // simulate queuing delay of 3sec

    // make sure we have a bandwidth estimate ready
    testNetStatsSimple();

    struct timeval zero = {0,0};
    struct timespec sleeptime1 = {0, 500 * 1000 * 1000};
    struct timespec sleeptime2 = {0, 540 * 1000 * 1000};
    struct timespec sleeptime3 = {2, 0 * 1000 * 1000};
    nowake_nanosleep(&sleeptime1);

    stats->report_send_event(3, 5000);

    nowake_nanosleep(&sleeptime1);

    stats->report_send_event(4, 10000);

    nowake_nanosleep(&sleeptime2);

    stats->report_ack(3, zero);
    
    nowake_nanosleep(&sleeptime3);

    stats->report_ack(4, zero);
    
    assertStatsCorrect(5000, 20);
}

void
EstimationTest::testNetStatsWithSenderDelay()
{
    testNetStatsSimple();
    
    struct timeval zero = {0,0};
    struct timespec sleeptime1 = {1, 300 * 1000 * 1000};
    struct timespec sleeptime2 = {1, 40 * 1000 * 1000};
    nowake_nanosleep(&sleeptime1);

    stats->report_send_event(5, 5000);

    nowake_nanosleep(&sleeptime1);
    stats->report_send_event(5, 5000);

    nowake_nanosleep(&sleeptime2);

    stats->report_ack(5, zero);

    assertStatsCorrect(5000, 20);
}

// There should be no queuing delay compensation between
//  messages in a single IROB.
void
EstimationTest::testNetStatsSingleIROBQueuingDelay()
{
    testNetStatsSimple();
    
    struct timeval zero = {0,0};
    struct timespec sleeptime1 = {0, 300 * 1000 * 1000};
    struct timespec sleeptime2 = {0, 700 * 1000 * 1000};
    struct timespec sleeptime3 = {1, 40 * 1000 * 1000};
    struct timespec sleeptime4 = {1, 0 * 1000 * 1000};
    nowake_nanosleep(&sleeptime1);

    stats->report_send_event(5, 5000);

    nowake_nanosleep(&sleeptime1);
    stats->report_send_event(5, 5000);

    nowake_nanosleep(&sleeptime2);
    stats->report_send_event(6, 5000);
    nowake_nanosleep(&sleeptime3);

    stats->report_ack(5, zero);
    nowake_nanosleep(&sleeptime4);
    stats->report_ack(6, zero);

    assertStatsCorrect(5000, 20);
}

void
EstimationTest::assertStatsCorrect(u_long expected_bw, 
                                   u_long expected_latency)
{
    u_long bw = 0, latency = 0;
    bool ret = stats->get_estimate(NET_STATS_BW_UP, bw);
    ret = ret && stats->get_estimate(NET_STATS_LATENCY, latency);
    CPPUNIT_ASSERT(ret);

    CPPUNIT_ASSERT_EQUAL_MESSAGE("Bandwidth estimate matches expected",
                                 expected_bw, bw);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("Latency estimate matches expected",
                                 expected_latency, latency);
}
