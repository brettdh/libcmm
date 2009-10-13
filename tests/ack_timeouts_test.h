#ifndef ack_timeouts_test_h_incl
#define ack_timeouts_test_h_incl

#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>
#include "ack_timeouts.h"

class AckTimeoutsTest : public CppUnit::TestFixture {
    AckTimeouts *ack_timeouts;

    CPPUNIT_TEST_SUITE(AckTimeoutsTest);
    CPPUNIT_TEST(testOrdering);
    CPPUNIT_TEST(testCornerCases);
    CPPUNIT_TEST_SUITE_END();

  public:
    void testOrdering();
    void testCornerCases();
};

#endif
