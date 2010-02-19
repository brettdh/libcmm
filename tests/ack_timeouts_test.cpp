#include <cppunit/Test.h>
#include <cppunit/TestAssert.h>
#include <cppunit/extensions/HelperMacros.h>
#include "ack_timeouts_test.h"
#include "timeops.h"
#include <vector>
using std::vector;

CPPUNIT_TEST_SUITE_REGISTRATION(AckTimeoutsTest);

void
AckTimeoutsTest::disabledReminder()
{
    fprintf(stderr, "Ack timeouts are currently disabled, "
            "so I've disabled their tests too.\n");
}

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

    tv.tv_sec = 2;
    tv.tv_nsec = 0;
    timeouts.update(2, tv);
    vec = timeouts.remove_expired();
    CPPUNIT_ASSERT_MESSAGE("No timeouts returned when none are expired",
                           vec.empty());

    tv.tv_sec = 1;
    timeouts.update(1, tv);
    tv.tv_sec = 2;
    timeouts.update(3, tv);

    sleep(3);
    vec = timeouts.remove_expired();
    CPPUNIT_ASSERT_EQUAL_MESSAGE("All timeouts returned", 3, (int)vec.size());
    for (int i = 0; i < 3; ++i) {
        CPPUNIT_ASSERT_EQUAL_MESSAGE("Correct IROB value", i+1, (int)vec[i]);
    }
}

void
AckTimeoutsTest::testRemove()
{
    AckTimeouts timeouts;
    struct timespec tv = {3, 0};

    timeouts.update(1, tv);
    timeouts.update(99, tv);
    timeouts.update(2, tv);
    timeouts.update(3, tv);
    
    tv.tv_sec = 5;
    timeouts.update(4, tv);
    timeouts.update(100, tv);
    timeouts.update(5, tv);

    timeouts.remove(99);
    sleep(3);
    vector<irob_id_t> vec = timeouts.remove_expired();
    CPPUNIT_ASSERT_EQUAL_MESSAGE("All timeouts returned", 3, (int)vec.size());
    for (int i = 0; i < 3; ++i) {
        CPPUNIT_ASSERT_EQUAL_MESSAGE("Correct IROB value", i+1, (int)vec[i]);
    }

    sleep(2);
    timeouts.remove(100);
    vec = timeouts.remove_expired();
    CPPUNIT_ASSERT_EQUAL_MESSAGE("All timeouts returned", 2, (int)vec.size());
    for (int i = 0; i < 2; ++i) {
        CPPUNIT_ASSERT_EQUAL_MESSAGE("Correct IROB value", i+4, (int)vec[i]);
    }
}
