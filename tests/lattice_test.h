#ifndef lattice_test_h_incl
#define lattice_test_h_incl

#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>
#include "pending_irob.h"

class LatticeTest : public CppUnit::TestFixture {
    PendingIROBLattice *pirobs;
    PendingIROB *pirob_array[10];

    CPPUNIT_TEST_SUITE(LatticeTest);
    CPPUNIT_TEST(testLatticeStructure);
    CPPUNIT_TEST(testRemoval);
    CPPUNIT_TEST_SUITE_END();

  public:
    void setUp();
    void tearDown();

    void assert_insert(irob_id_t id, PendingIROB *pirob);

    void testLatticeStructure();
    void testRemoval();
};

#endif
