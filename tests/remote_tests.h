#ifndef remote_tests_h_incl
#define remote_tests_h_incl

#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>
#include "end_to_end_tests_remote.h"

class RemoteTests : public EndToEndTestsRemote {
    CPPUNIT_TEST_SUITE(RemoteTests);
    CPPUNIT_TEST(testRandomBytesReceivedCorrectly);
    CPPUNIT_TEST(testDefaultIROBOrdering);
    CPPUNIT_TEST(testNoInterleaving);
    CPPUNIT_TEST(testPartialRecv);
    CPPUNIT_TEST(testCMMPoll);
    //CPPUNIT_TEST(testHalfShutdown);
    CPPUNIT_TEST_SUITE_END();

  protected:
    void testDefaultIROBOrdering();
};

#endif
