#include <cppunit/TestFixture.h>
#include "pending_irob.h"

class LatticeTest : public CppUnit::TestFixture {
    PendingIROBLattice *pirobs;
    PendingIROB *pirob_array[10];
  public:
    static CppUnit::Test *suite();

    void setUp();
    void tearDown();

    void assert_insert(irob_id_t id, PendingIROB *pirob);
    void testLatticeStructure();
    void testRemoval();
};
