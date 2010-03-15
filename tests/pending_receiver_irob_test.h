#ifndef pending_receiver_irob_test_h_incl
#define pending_receiver_irob_test_h_incl

#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>
#include "pending_receiver_irob.h"

const size_t BUFSIZE = 50;

class PendingReceiverIROBTest : public CppUnit::TestFixture {
    PendingReceiverIROB *prirob;

    CPPUNIT_TEST_SUITE(PendingReceiverIROBTest);
    //CPPUNIT_TEST(testOverwrite);
    CPPUNIT_TEST_SUITE_END();

    static char buffer[BUFSIZE+1];

  public:
    void setUp();
    void tearDown();

    void testOverwrite();
};

#endif
