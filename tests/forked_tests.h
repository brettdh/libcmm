#ifndef forked_tests_h_incl
#define forked_tests_h_incl

#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>
#include "end_to_end_tests_forked.h"
#include <sys/wait.h>

class ForkedTests : public EndToEndTestsForked {

    CPPUNIT_TEST_SUITE(ForkedTests);
    CPPUNIT_TEST(testRandomBytesReceivedCorrectly);
    CPPUNIT_TEST(testOrderingSimple);
    CPPUNIT_TEST(testOrderingReverse);
    CPPUNIT_TEST(testNoInterleaving);
    CPPUNIT_TEST(testCMMPoll);
    CPPUNIT_TEST_SUITE_END();
};

#endif
