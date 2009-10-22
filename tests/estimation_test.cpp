#include <cppunit/Test.h>
#include <cppunit/TestAssert.h>
#include <cppunit/extensions/HelperMacros.h>
#include "estimation_test.h"

CPPUNIT_TEST_SUITE_REGISTRATION(EstimationTest);

void
EstimationTest::setUp()
{
    estimate = new Estimate;
}

void
EstimationTest::tearDown()
{
    delete estimate;
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
