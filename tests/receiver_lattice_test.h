#include <cppunit/TestFixture.h>
#include "pending_receiver_irob.h"

class ReceiverLatticeTest : public CppUnit::TestFixture {
    PendingReceiverIROBLattice *pirobs;
    PendingReceiverIROB *pirob_array[4];
  public:
    static CppUnit::Test *suite();

    void setUp();
    void tearDown();

    void assert_insert(irob_id_t id, PendingReceiverIROB *pirob);
    void testReceive();
};
