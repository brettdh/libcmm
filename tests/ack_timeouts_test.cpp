#include <cppunit/Test.h>
#include <cppunit/TestAssert.h>
#include <cppunit/extensions/HelperMacros.h>
#include "ack_timeouts_test.h"
#include "timeops.h"
#include <vector>
using std::vector;

CPPUNIT_TEST_SUITE_REGISTRATION(AckTimeoutsTest);

void 
AckTimeoutsTest::testOrdering()
{
    AckTimeouts timeouts;

    struct timespec rel = {1, 0};
    struct timespec before, after;
    TIME(before);
    timeouts.update(1, rel);
    TIME(after);
    timeradd(&after, &rel, &after);
    timeouts.update(2, rel);
    timeouts.update(3, rel);

    struct timespec now;
    CPPUNIT_ASSERT_MESSAGE("Found earliest time", 
                           timeouts.get_earliest(now));
    CPPUNIT_ASSERT_MESSAGE("Earliest time is as expected (secs)",
                           timercmp(&before, &now, <));
    CPPUNIT_ASSERT_MESSAGE("Earliest time is as expected (secs)",
                           timercmp(&now, &after, <));

    sleep(2);

    timeouts.update(4, rel);
    vector<irob_id_t> ret = timeouts.remove_expired();
    CPPUNIT_ASSERT_EQUAL_MESSAGE("Got the right number of expired timeouts",
                                 3, (int)ret.size());
    for (int i = 0; i < 3; ++i) {
        CPPUNIT_ASSERT_EQUAL_MESSAGE("Correct IROB value", i+1, (int)ret[i]);
    }

    sleep(1);
    ret = timeouts.remove_expired();

    CPPUNIT_ASSERT_EQUAL_MESSAGE("Got the right number of expired timeouts",
                                 1, (int)ret.size());
    CPPUNIT_ASSERT_EQUAL_MESSAGE("Correct IROB value", 4, (int)ret[0]);
    
}

void
AckTimeoutsTest::testCornerCases()
{
    AckTimeouts timeouts;

    struct timespec tv;
    bool ret = timeouts.get_earliest(tv);
    CPPUNIT_ASSERT_MESSAGE("No timeout returned for empty structure", !ret);
    
    vector<irob_id_t> vec = timeouts.remove_expired();
    CPPUNIT_ASSERT_MESSAGE("No expired timeouts returned for empty structure",
                           vec.empty());

    tv.tv_sec = 10;
    tv.tv_nsec = 0;
    timeouts.update(1, tv);
    vec = timeouts.remove_expired();
    CPPUNIT_ASSERT_MESSAGE("No timeouts returned when none are expired",
                           vec.empty());
}
