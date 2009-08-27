#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>
#include "pending_receiver_irob.h"

class ReceiverLatticeTest : public CppUnit::TestFixture {
    PendingReceiverIROBLattice *pirobs;
    PendingReceiverIROB *pirob_array[4];

    CPPUNIT_TEST_SUITE(ReceiverLatticeTest);
    CPPUNIT_TEST(testReceive);
    CPPUNIT_TEST(testMultiIROBReceive);
    CPPUNIT_TEST_SUITE_END();

  public:
    void setUp();
    void tearDown();

    void assert_insert(irob_id_t id, PendingReceiverIROB *pirob);
    void testReceive();
    void testMultiIROBReceive();
};
