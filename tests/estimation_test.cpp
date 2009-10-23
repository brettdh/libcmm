#include <cppunit/Test.h>
#include <cppunit/TestAssert.h>
#include <cppunit/extensions/HelperMacros.h>
#include "estimation_test.h"
#include "timeops.h"

CPPUNIT_TEST_SUITE_REGISTRATION(EstimationTest);

void
EstimationTest::setUp()
{
    estimate = new Estimate;
    delays = new QueuingDelay;
}

void
EstimationTest::tearDown()
{
    delete estimate;
    delete delays;
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

    CPPUNIT_ASSERT_EQUAL_MESSAGE("Returns agile estimate at first",
                                 (u_long)agile_estimate, 
                                 estimate->get_estimate());

    spot_value = 2000.0;
    update_EWMA(stable_estimate, spot_value, STABLE_GAIN);
    update_EWMA(agile_estimate, spot_value, AGILE_GAIN);
    estimate->add_observation(spot_value);
    printf("stable: %lu agile: %lu flipflop: %lu\n",
           (u_long)stable_estimate, (u_long)agile_estimate,
           (u_long)estimate->get_estimate());

    CPPUNIT_ASSERT_EQUAL_MESSAGE("Returns stable estimate "
                                 "when variance is too big",
                                 (u_long)stable_estimate, 
                                 estimate->get_estimate());

    for (int i = 0; i < 20; ++i) {
        update_EWMA(stable_estimate, spot_value, STABLE_GAIN);
        update_EWMA(agile_estimate, spot_value, AGILE_GAIN);
        estimate->add_observation(spot_value);

        printf("stable: %lu agile: %lu flipflop: %lu\n",
               (u_long)stable_estimate, (u_long)agile_estimate,
               (u_long)estimate->get_estimate());
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
