#include <cppunit/Test.h>
#include <cppunit/TestAssert.h>
#include <cppunit/extensions/HelperMacros.h>
#include "estimation_test.h"
#include "timeops.h"
#include "test_common.h"
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include "net_interface.h"

CPPUNIT_TEST_SUITE_REGISTRATION(EstimationTest);

void
EstimationTest::setUp()
{
    estimate = new FlipFlopEstimate("test");
    delays = new QueuingDelay;

    struct net_interface dummy;
    memset(&dummy, 0, sizeof(dummy));
    stats = new NetStats(dummy, dummy);
    
    NetStats::resetAll();

    struct net_interface other;
    memset(&other, 0, sizeof(other));
    CPPUNIT_ASSERT(inet_aton("42.42.42.42", &other.ip_addr) != 0);
    other_stats = new NetStats(other, other);
}

void
EstimationTest::tearDown()
{
    delete estimate;
    delete delays;
    delete stats;
    delete other_stats;
}

#define STABLE_GAIN 0.9
#define AGILE_GAIN 0.1

void
EstimationTest::testFlipFlop()
{
    double agile_estimate = 0.0;
    double stable_estimate = 0.0;

    double spot_value = 0.0;
    for (int i = 0; i < 11; ++i) {
        spot_value += (i % 2 == 0 ? 1 : -1);
        update_EWMA(stable_estimate, spot_value, STABLE_GAIN);
        update_EWMA(agile_estimate, spot_value, AGILE_GAIN);
        estimate->add_observation((u_long)spot_value);
    }

    double est_value;
    bool ret = estimate->get_estimate(est_value);
    CPPUNIT_ASSERT(ret);
    MY_CPPUNIT_ASSERT_EQWITHIN_MESSAGE(agile_estimate,
                                       est_value,
                                       1.0,
                                       "Returns agile estimate at first");

    spot_value = 2000.0;
    update_EWMA(stable_estimate, spot_value, STABLE_GAIN);
    update_EWMA(agile_estimate, spot_value, AGILE_GAIN);
    estimate->add_observation((u_long)spot_value);

    ret = estimate->get_estimate(est_value);
    CPPUNIT_ASSERT(ret);    
    printf("stable: %f agile: %f flipflop: %f\n",
           stable_estimate, 
           agile_estimate,
           est_value);

    MY_CPPUNIT_ASSERT_EQWITHIN_MESSAGE(stable_estimate, 
                                       est_value, 1.0,
                                       "Returns stable estimate "
                                       "when variance is too big");

    for (int i = 0; i < 20; ++i) {
        update_EWMA(stable_estimate, spot_value, STABLE_GAIN);
        update_EWMA(agile_estimate, spot_value, AGILE_GAIN);
        estimate->add_observation(spot_value);

        ret = estimate->get_estimate(est_value);
        CPPUNIT_ASSERT(ret);    
        printf("stable: %f agile: %f flipflop: %f\n",
               stable_estimate,
               agile_estimate,
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

    stats->report_total_irob_bytes(1, 5000);
    stats->report_irob_send_event(1, 5000);
    nowake_nanosleep(&sleeptime);
    stats->report_ack(1, zero, zero, NULL);
    
    sleeptime.tv_sec = 2;
    stats->report_total_irob_bytes(2, 10000);
    stats->report_irob_send_event(2, 10000);
    nowake_nanosleep(&sleeptime);
    stats->report_ack(2, zero, zero, NULL);
    
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

    stats->report_total_irob_bytes(3, 5000);
    stats->report_irob_send_event(3, 5000);

    nowake_nanosleep(&sleeptime1);

    stats->report_total_irob_bytes(4, 10000);
    stats->report_irob_send_event(4, 10000);

    nowake_nanosleep(&sleeptime2);

    stats->report_ack(3, zero, zero, NULL);
    
    nowake_nanosleep(&sleeptime3);

    stats->report_ack(4, zero, zero, NULL);
    
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

    stats->report_irob_send_event(5, 5000);

    nowake_nanosleep(&sleeptime1);
    stats->report_total_irob_bytes(5, 5000);
    stats->report_irob_send_event(5, 5000);

    nowake_nanosleep(&sleeptime2);

    stats->report_ack(5, zero, zero, NULL);

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

    stats->report_irob_send_event(5, 5000);

    nowake_nanosleep(&sleeptime1);
    stats->report_total_irob_bytes(5, 10000);
    stats->report_irob_send_event(5, 5000);

    nowake_nanosleep(&sleeptime2);
    stats->report_total_irob_bytes(6, 5000);
    stats->report_irob_send_event(6, 5000);
    nowake_nanosleep(&sleeptime3);

    stats->report_ack(5, zero, zero, NULL);
    nowake_nanosleep(&sleeptime4);
    stats->report_ack(6, zero, zero, NULL);

    assertStatsCorrect(5000, 20);
}

void
EstimationTest::testQueuingDelayInterleaved()
{
    testNetStatsSimple();
    
    struct timeval zero = {0,0};
    struct timespec sleeptime1 = {0, 300 * 1000 * 1000};
    struct timespec sleeptime2 = {0, 700 * 1000 * 1000};
    struct timespec sleeptime3 = {1, 40 * 1000 * 1000};
    struct timespec sleeptime4 = {1, 0 * 1000 * 1000};
    nowake_nanosleep(&sleeptime1);

    // 0:00.00
    stats->report_irob_send_event(5, 5000);
    nowake_nanosleep(&sleeptime1);

    // 0:00.30
    stats->report_irob_send_event(5, 5000);
    nowake_nanosleep(&sleeptime2);

    // 0:01.00
    stats->report_irob_send_event(6, 5000);
    stats->report_total_irob_bytes(5, 15000);
    stats->report_irob_send_event(5, 5000);
    nowake_nanosleep(&sleeptime4);
    // 0:02.00
    nowake_nanosleep(&sleeptime4);

    // 0:03.00
    stats->report_total_irob_bytes(6, 10000);
    stats->report_irob_send_event(6, 5000);
    nowake_nanosleep(&sleeptime3);

    // 0:04.04
    stats->report_ack(5, zero, zero, NULL); // RTT = 4.04 - 0.0 - 1.0 = 3.
    nowake_nanosleep(&sleeptime4);

    // 0:05.04
    stats->report_ack(6, zero, zero, NULL);

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

    // CPPUNIT_ASSERT_EQUAL_MESSAGE("Bandwidth estimate matches expected",
    //                                  expected_bw, bw);
    //     CPPUNIT_ASSERT_EQUAL_MESSAGE("Latency estimate matches expected",
    //                                  expected_latency, latency);

    MY_CPPUNIT_ASSERT_EQWITHIN_MESSAGE(expected_bw, bw, 0.05,
                                       "Bandwidth estimate matches expected");
    MY_CPPUNIT_ASSERT_EQWITHIN_MESSAGE(expected_latency, latency, 0.15,
                                       "Latency estimate matches expected");
}

void
EstimationTest::testDisregardStripedIROBs()
{
    // true bw: 5000 Bps  true latency: 20ms
    
    // make sure we have a bandwidth estimate ready
    testNetStatsSimple();

    struct timeval zero = {0,0};
    struct timespec sleeptime1 = {0, 10 * 1000 * 1000};
    struct timespec sleeptime2 = {0, 40 * 1000 * 1000};
    nowake_nanosleep(&sleeptime1);

    stats->report_total_irob_bytes(3, 15000);
    stats->report_irob_send_event(3, 5000);

    nowake_nanosleep(&sleeptime1);

    other_stats->report_irob_send_event(3, 10000);

    nowake_nanosleep(&sleeptime2);

    stats->report_ack(3, zero, zero, NULL);

    // this IROB should be ignored, so the stats should be the same as before.
    assertStatsCorrect(5000, 20);
}

void
EstimationTest::testFailoverDelay()
{
    // true bw: 5000 Bps  true latency: 20ms
    
    // make sure we have some initial stats
    testNetStatsSimple();

    bool failover = stats->mark_irob_failures(NULL, 0);
    CPPUNIT_ASSERT(!failover);

    stats->report_total_irob_bytes(3, 1500);
    stats->report_irob_send_event(3, 1500);
    
    double expected_failover_delay = 5.0; // seconds
    struct timespec sleeptime = {(time_t) expected_failover_delay, 0};
    nowake_nanosleep(&sleeptime);

    double failover_delay = 0.0;
    failover = stats->mark_irob_failures(NULL, 0, &failover_delay);
    CPPUNIT_ASSERT(failover);
    
    // returned value is latency (half-RTT)
    MY_CPPUNIT_ASSERT_EQWITHIN_MESSAGE(expected_failover_delay, failover_delay * 2.0, 0.1,
                                       "Failover delay matches expected");

    
    // test that max of all failover delays is reported
    sleeptime.tv_sec = 1;

    stats->report_total_irob_bytes(4, 2000);
    stats->report_irob_send_event(4, 2000);

    nowake_nanosleep(&sleeptime);

    stats->report_total_irob_bytes(5, 1500);
    stats->report_irob_send_event(5, 1500);

    nowake_nanosleep(&sleeptime);

    stats->report_total_irob_bytes(6, 1750);
    stats->report_irob_send_event(6, 1750);
    
    nowake_nanosleep(&sleeptime);

    // should be 3 seconds of failover
    
    expected_failover_delay = 3.0;
    failover_delay = 0.0;
    failover = stats->mark_irob_failures(NULL, 0, &failover_delay);
    CPPUNIT_ASSERT(failover);
    
    // returned value is latency (half-RTT)
    MY_CPPUNIT_ASSERT_EQWITHIN_MESSAGE(expected_failover_delay, failover_delay * 2.0, 0.1,
                                       "Failover delay matches expected");
}
