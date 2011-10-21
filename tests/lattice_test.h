#ifndef lattice_test_h_incl
#define lattice_test_h_incl

#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>
#include "pending_irob.h"

#include <set>

class LatticeTest : public CppUnit::TestFixture {
    PendingIROBLattice *pirobs;
    PendingIROB *pirob_array[10];

    CPPUNIT_TEST_SUITE(LatticeTest);
    CPPUNIT_TEST(testLatticeStructure);
    CPPUNIT_TEST(testRemoval);
    CPPUNIT_TEST(testTransitiveDropIROB);
    CPPUNIT_TEST_SUITE_END();

  public:
    void setUp();
    void tearDown();

    void testLatticeStructure();
    void testRemoval();
    void testTransitiveDropIROB();
  private:
    void assert_insert(irob_id_t id, PendingIROB *pirob);
    void assert_contents(const std::set<irob_id_t>& present_irobs,
                         const std::set<irob_id_t>& absent_irobs);
    void init_dep_matrix(bool dep_matrix[10][10]);
        
};

#endif
