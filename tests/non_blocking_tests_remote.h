#ifndef non_blocking_tests_remote_h_incl
#define non_blocking_tests_remote_h_incl

#include <cppunit/TestFixture.h>
#include <libcmm.h>
#include "end_to_end_tests_remote.h"
#include "non_blocking_tests.h"

class NonBlockingTestsRemote : public EndToEndTestsRemote, 
                               public NonBlockingTestsBase {
  public:
    CPPUNIT_TEST_SUITE(NonBlockingTestsRemote);
    CPPUNIT_TEST(testTransfer);
    CPPUNIT_TEST(testFragmentation);
    CPPUNIT_TEST_SUITE_END();

  protected:
    DECLARE_WRAPPER_METHODS()
};

#endif
