#include <cppunit/Test.h>
#include <cppunit/TestAssert.h>
#include <cppunit/extensions/HelperMacros.h>
#include "lattice_test.h"
#include <sstream>
using std::ostringstream;

CPPUNIT_TEST_SUITE_REGISTRATION(LatticeTest);

void
LatticeTest::setUp()
{
    pirobs = new PendingIROBLattice();

    irob_id_t id = 0;
    pirob_array[0] = new PendingIROB(0, 0, NULL, 0, NULL, 0);
    pirob_array[1] = new PendingIROB(1, 1, &id, 0, NULL, 0);
    pirob_array[2] = new PendingIROB(2, 1, &id, 0, NULL, 0);
    pirob_array[3] = new PendingIROB(3, 0, NULL, 0, NULL, 0);
    pirob_array[4] = new PendingIROB(4, 0, NULL, 20, new char[20], 0);
    pirob_array[5] = new PendingIROB(5, 0, NULL, 0, NULL, 0);
    pirob_array[6] = new PendingIROB(6, 0, NULL, 0, NULL, 0);
    id = 6;
    pirob_array[7] = new PendingIROB(7, 1, &id, 0, NULL, 0);
    pirob_array[8] = new PendingIROB(8, 0, NULL, 20, new char[20], 0);
    pirob_array[9] = new PendingIROB(9, 0, NULL, 20, new char[20], 0);
}

void
LatticeTest::tearDown()
{
    pirobs->clear();
    delete pirobs;
}

void
LatticeTest::assert_insert(irob_id_t id, PendingIROB *pirob)
{
    CPPUNIT_ASSERT(pirobs->insert(pirob) == true);
    CPPUNIT_ASSERT(get_pointer(pirobs->find(id)) == pirob);
}

void
LatticeTest::init_dep_matrix(bool dep_matrix[][])
{
    for (int i = 0; i < 10; i++) {
        for (int j = 0; j < 10; j++) {
            dep_matrix[i][j] = false;
        }
    }
    
    const int NUM_DEPS = 11;
    int dep_pairs[NUM_DEPS][2] = {
        {1,0}, {2,0}, {4,1}, {4,2}, {4,3}, 
        {5,4}, {6,4}, {7,6}, {8,5}, {8,7}, {9,8}
    };

    for (int i = 0; i < NUM_DEPS; i++) {
        dep_matrix[dep_pairs[i][0]][dep_pairs[i][1]] = true;
    }
}

void 
LatticeTest::testLatticeStructure()
{
    for (int i = 0; i < 10; i++) {
        assert_insert(i, pirob_array[i]);
    }

    bool dep_matrix[10][10];
    init_dep_matrix(dep_matrix);
    
    for (int i = 0; i < 10; i++) {
        for (int j = 0; j < 10; j++) {
            ostringstream oss;
            oss << "IROB " << i << " should";
            if (!dep_matrix[i][j]) {
                oss << "n't";
            }
            oss << " depend on IROB " << j << ", but does";
            if (dep_matrix[i][j]) {
                oss << "n't";
            }
            CPPUNIT_ASSERT_MESSAGE(oss.str(),
                                   pirob_array[i]->depends_on(j) 
                                   == dep_matrix[i][j]);
        }
    }
}

// XXX: this test is WRONG for the lattice on the sender side.
// XXX: hence the second argument to erase calls here.
void
LatticeTest::testRemoval()
{
    testLatticeStructure();
    
    CPPUNIT_ASSERT(pirobs->erase(0, true) == true);
    CPPUNIT_ASSERT(pirob_array[1]->depends_on(0) == false);
    CPPUNIT_ASSERT(pirob_array[2]->depends_on(0) == false);

    CPPUNIT_ASSERT(pirobs->erase(1, true) == true);
    CPPUNIT_ASSERT(pirob_array[4]->depends_on(1) == false);

    CPPUNIT_ASSERT(pirobs->erase(2, true) == true);
    CPPUNIT_ASSERT(pirob_array[4]->depends_on(2) == false);

    CPPUNIT_ASSERT(pirobs->erase(3, true) == true);
    CPPUNIT_ASSERT(pirob_array[4]->depends_on(3) == false);

    CPPUNIT_ASSERT(pirobs->erase(4, true) == true);
    CPPUNIT_ASSERT(pirob_array[5]->depends_on(4) == false);
    CPPUNIT_ASSERT(pirob_array[6]->depends_on(4) == false);
    
    CPPUNIT_ASSERT(pirobs->erase(6, true) == true);
    CPPUNIT_ASSERT(pirob_array[7]->depends_on(6) == false);

    CPPUNIT_ASSERT(pirobs->erase(5, true) == true);
    CPPUNIT_ASSERT(pirob_array[8]->depends_on(5) == false);

    CPPUNIT_ASSERT(pirobs->erase(7, true) == true);
    CPPUNIT_ASSERT(pirob_array[8]->depends_on(7) == false);

    CPPUNIT_ASSERT(pirobs->erase(8, true) == true);
    CPPUNIT_ASSERT(pirob_array[9]->depends_on(8) == false);

    CPPUNIT_ASSERT(pirobs->erase(9, true) == true);
}

void
LatticeTest::assert_contents(const set<irob_id_t>& present_irobs,
                             const set<irob_id_t>& absent_irobs)
{
    for (irob_id_t::const_iterator it = present_irobs.begin();
         it != present_irobs.end(); ++it) {
        CPPUNIT_ASSERT(pirobs->find(*it));
    }
    for (irob_id_t::const_iterator it = absent_irobs.begin();
         it != absent_irobs.end(); ++it) {
        CPPUNIT_ASSERT(!pirobs->find(*it));
    }
}

void
LatticeTest::testTransitiveDropIROB()
{
    testLatticeStructure();
    
    bool dep_matrix[10][10];
    init_dep_matrix(dep_matrix);
    
    // dep chain at this point:
    //     0    3   4   5   6   8   9
    //    / \               |
    //   1   2              7

    set<irob_id_t> present_irobs, absent_irobs;
    for (int i = 0; i < 10; ++i) {
        present_irobs.insert(i);
    }
    assert_contents(present_irobs, absent_irobs);

    pirobs->drop_irob_and_dependents(0);
    for (int i = 0; i <= 2; ++i) {
        present_irobs.erase(i);
        absent_irobs.insert(i);
    }
    assert_contents(present_irobs, absent_irobs);

    pirobs->drop_irob_and_dependents(3);
    present_irobs.erase(3);
    absent_irobs.insert(3);
    assert_contents(present_irobs, absent_irobs);

    pirobs->drop_irob_and_dependents(6);
    for (int i = 6; i <= 7; ++i) {
        present_irobs.erase(i);
        absent_irobs.insert(i);
    }
    assert_contents(present_irobs, absent_irobs);

    // dep chain at this point:
    //     4   5   8   9
    
    irob_id_t id = 9;
    pirobs->insert(new PendingIROB(10, 1, &id, 0, NULL, 0));
    id = 10;
    pirobs->insert(new PendingIROB(11, 1, &id, 0, NULL, 0));
    present_irobs.insert(10);
    present_irobs.insert(11);
    assert_contents(present_irobs, absent_irobs);

    // dep chain at this point:
    //     4   5   8   9
    //                 |
    //                 10
    //                 |
    //                 11

    pirobs->drop_irob_and_dependents(9);
    for (int i = 9; i <= 11; ++i) {
        present_irobs.erase(i);
        absent_irobs.insert(i);
    }
    assert_contents(present_irobs, absent_irobs);
}
