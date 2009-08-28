#ifndef end_to_end_tests_forked_h_incl
#define end_to_end_tests_forked_h_incl

#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>
#include "end_to_end_tests_base.h"

class EndToEndTestsForked : public EndToEndTestsBase {
    static pid_t receiver_pid; // 0 if it's me

    CPPUNIT_TEST_SUITE(EndToEndTestsForked);
    CPPUNIT_TEST(testRandomBytesReceivedCorrectly);
    CPPUNIT_TEST(testOrderingSimple);
    CPPUNIT_TEST(testOrderingReverse);
    CPPUNIT_TEST_SUITE_END();

  protected:
    virtual void chooseRole();
    virtual bool isReceiver();
    virtual void waitForReceiver();

    //void testRandomBytesReceivedCorrectly();
    void testOrderingSimple();
    void testOrderingReverse();
};

#endif
