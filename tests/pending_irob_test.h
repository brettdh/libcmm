#include <cppunit/TestFixture.h>
#include "pending_irob.h"

class PendingIROBLatticeTest : public CppUnit::TestFixture {
    PendingIROBLattice *pirobs;
    PendingIROB *pirob0;
    PendingIROB *pirob1;
    PendingIROB *pirob2;
    PendingIROB *pirob3;
  public:
    static CppUnit::Test *suite();

    void setUp();
    void tearDown();

    void testInsert();
    void testErase();
    void testHoles();
};
