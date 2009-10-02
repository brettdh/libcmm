#ifndef trickle_tests_h_incl
#define trickle_tests_h_incl

#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>
#include "end_to_end_tests_remote.h"

class TrickleTests : public EndToEndTestsRemote {
    static bool is_receiver;
  
    CPPUNIT_TEST_SUITE(TrickleTests);
    CPPUNIT_TEST(testTrickle);
    CPPUNIT_TEST_SUITE_END();

  protected:
    void testTrickle();
};

#endif
