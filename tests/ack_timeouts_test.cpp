#include <cppunit/Test.h>
#include <cppunit/TestAssert.h>
#include <cppunit/extensions/HelperMacros.h>
#include "ack_timeouts_test.h"
#include "timeops.h"
#include <vector>
using std::vector;

CPPUNIT_TEST_SUITE_REGISTRATION(AckTimeoutsTest);

void 
AckTimeoutsTest::setUp()
{
    ack_timeouts = new AckTimeouts;
}

void 
AckTimeoutsTest::tearDown()
{
    delete ack_timeouts;
}

void 
AckTimeoutsTest::testOrdering()
{
    struct timespec rel = {1, 0};
    struct timespec before, after;
    TIME(before);
    ack_timeouts->update(1, rel);
    TIME(after);
    timeradd(&after, &rel, &after);
    ack_timeouts->update(2, rel);
    ack_timeouts->update(3, rel);

    struct timespec now;
    CPPUNIT_ASSERT_MESSAGE("Found earliest time", 
                           ack_timeouts->get_earliest(now));
    CPPUNIT_ASSERT_MESSAGE("Earliest time is as expected (secs)",
                           timercmp(&before, &now, <));
    CPPUNIT_ASSERT_MESSAGE("Earliest time is as expected (secs)",
                           timercmp(&now, &after, <));

    sleep(2);

    ack_timeouts->update(4, rel);
    vector<irob_id_t> ret = ack_timeouts->remove_expired();
    CPPUNIT_ASSERT_EQUAL_MESSAGE("Got the right number of expired timeouts",
                                 3, (int)ret.size());
    for (int i = 0; i < 3; ++i) {
        CPPUNIT_ASSERT_EQUAL_MESSAGE("Correct IROB value", i+1, (int)ret[i]);
    }

    sleep(1);
    ret = ack_timeouts->remove_expired();

    CPPUNIT_ASSERT_EQUAL_MESSAGE("Got the right number of expired timeouts",
                                 1, (int)ret.size());
    CPPUNIT_ASSERT_EQUAL_MESSAGE("Correct IROB value", 4, (int)ret[0]);
    
}
