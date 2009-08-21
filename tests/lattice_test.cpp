#include <cppunit/Test.h>
#include <cppunit/TestAssert.h>
#include <cppunit/TestSuite.h>
#include <cppunit/TestCaller.h>
#include "lattice_test.h"
#include <sstream>
using std::ostringstream;

CppUnit::Test *
LatticeTest::suite()
{
    CppUnit::TestSuite *testSuite = new CppUnit::TestSuite("LatticeTest");
    testSuite->addTest(new CppUnit::TestCaller<LatticeTest>(
                           "testLatticeStructure", 
                           &LatticeTest::testLatticeStructure));
    testSuite->addTest(new CppUnit::TestCaller<LatticeTest>(
                           "testRemoval", 
                           &LatticeTest::testRemoval));
    return testSuite;
}

void
LatticeTest::setUp()
{
    pirobs = new PendingIROBLattice();

    irob_id_t id = 0;
    pirob_array[0] = new PendingIROB(0, 0, NULL, 0, 0);
    pirob_array[1] = new PendingIROB(1, 1, &id, 0, 0);
    pirob_array[2] = new PendingIROB(2, 1, &id, 0, 0);
    pirob_array[3] = new PendingIROB(3, 0, NULL, 0, 0);
    pirob_array[4] = new PendingIROB(4, 20, new char[20], 0, 0);
    pirob_array[5] = new PendingIROB(5, 0, NULL, 0, 0);
    pirob_array[6] = new PendingIROB(6, 0, NULL, 0, 0);
    id = 6;
    pirob_array[7] = new PendingIROB(7, 1, &id, 0, 0);
    pirob_array[8] = new PendingIROB(8, 20, new char[20], 0, 0);
    pirob_array[9] = new PendingIROB(9, 20, new char[20], 0, 0);
}

void
LatticeTest::tearDown()
{
    for (int i = 0; i < 10; i++) {
        delete pirob_array[i];
    }

    pirobs->clear();
    delete pirobs;
}

void
LatticeTest::assert_insert(irob_id_t id, PendingIROB *pirob)
{
    CPPUNIT_ASSERT(pirobs->insert(pirob) == true);
    CPPUNIT_ASSERT(pirobs->find(id) == pirob);
}

void 
LatticeTest::testLatticeStructure()
{
    for (int i = 0; i < 10; i++) {
        assert_insert(i, pirob_array[i]);
    }

    bool dep_matrix[10][10];
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

void
LatticeTest::testRemoval()
{
    testLatticeStructure();
    
    CPPUNIT_ASSERT(pirobs->erase(0) == true);
    CPPUNIT_ASSERT(pirob_array[1]->depends_on(0) == false);
    CPPUNIT_ASSERT(pirob_array[2]->depends_on(0) == false);

    CPPUNIT_ASSERT(pirobs->erase(1) == true);
    CPPUNIT_ASSERT(pirob_array[4]->depends_on(1) == false);

    CPPUNIT_ASSERT(pirobs->erase(2) == true);
    CPPUNIT_ASSERT(pirob_array[4]->depends_on(2) == false);

    CPPUNIT_ASSERT(pirobs->erase(3) == true);
    CPPUNIT_ASSERT(pirob_array[4]->depends_on(3) == false);

    CPPUNIT_ASSERT(pirobs->erase(4) == true);
    CPPUNIT_ASSERT(pirob_array[5]->depends_on(4) == false);
    CPPUNIT_ASSERT(pirob_array[6]->depends_on(4) == false);
    
    CPPUNIT_ASSERT(pirobs->erase(6) == true);
    CPPUNIT_ASSERT(pirob_array[7]->depends_on(6) == false);

    CPPUNIT_ASSERT(pirobs->erase(5) == true);
    CPPUNIT_ASSERT(pirob_array[8]->depends_on(5) == false);

    CPPUNIT_ASSERT(pirobs->erase(7) == true);
    CPPUNIT_ASSERT(pirob_array[8]->depends_on(7) == false);

    CPPUNIT_ASSERT(pirobs->erase(8) == true);
    CPPUNIT_ASSERT(pirob_array[9]->depends_on(8) == false);

    CPPUNIT_ASSERT(pirobs->erase(9) == true);
}