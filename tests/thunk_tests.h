#ifndef thunk_tests_h_incl
#define thunk_tests_h_incl

#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>
#include "end_to_end_tests_remote.h"

class ThunkTests : public EndToEndTestsRemote {
    static bool is_receiver;
  
    CPPUNIT_TEST_SUITE(ThunkTests);
    CPPUNIT_TEST(testThunks);
    //CPPUNIT_TEST(testBlockingSend);
    CPPUNIT_TEST_SUITE_END();

  protected:
    void testThunks();
    void testBlockingSend();
};

#endif
