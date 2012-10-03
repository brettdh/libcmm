#include <cppunit/Test.h>
#include <cppunit/TestAssert.h>
#include <cppunit/extensions/HelperMacros.h>
#include "pending_irob_test.h"

CPPUNIT_TEST_SUITE_REGISTRATION(PendingIROBLatticeTest);

void 
PendingIROBLatticeTest::setUp()
{
    pirobs = new PendingIROBLattice();
    pirob0 = new PendingIROB(0, 0, NULL, 20, new char[20], 0);
    pirob1 = new PendingIROB(1, 0, NULL, 20, new char[20], 0);
    pirob2 = new PendingIROB(2, 0, NULL, 20, new char[20], 0);
    pirob3 = new PendingIROB(3, 0, NULL, 20, new char[20], 0);
}

void 
PendingIROBLatticeTest::tearDown()
{
    pirobs->clear();
    delete pirobs;
}

void 
PendingIROBLatticeTest::testInsert()
{
    CPPUNIT_ASSERT(pirobs->empty() == true);
    CPPUNIT_ASSERT(pirobs->insert(pirob0) == true);
    CPPUNIT_ASSERT(get_pointer(pirobs->find(0)) == pirob0);
    CPPUNIT_ASSERT(pirobs->insert(pirob1) == true);
    CPPUNIT_ASSERT(get_pointer(pirobs->find(1)) == pirob1);
    CPPUNIT_ASSERT(pirobs->insert(pirob1) == false);
    CPPUNIT_ASSERT(pirobs->empty() == false);
}

void 
PendingIROBLatticeTest::testErase()
{
    CPPUNIT_ASSERT(pirobs->empty() == true);
    CPPUNIT_ASSERT(pirobs->insert(pirob0) == true);
    CPPUNIT_ASSERT(get_pointer(pirobs->find(0)) == pirob0);
    CPPUNIT_ASSERT(pirobs->insert(pirob1) == true);
    CPPUNIT_ASSERT(get_pointer(pirobs->find(1)) == pirob1);
    
    CPPUNIT_ASSERT(pirobs->empty() == false);
    
    CPPUNIT_ASSERT(pirobs->erase((irob_id_t) 0) == true);
    CPPUNIT_ASSERT(get_pointer(pirobs->find(0)) == NULL);
    CPPUNIT_ASSERT(pirobs->erase((irob_id_t) 0) == false);
    
    CPPUNIT_ASSERT(get_pointer(pirobs->find(1)) == pirob1);
    CPPUNIT_ASSERT(pirobs->empty() == false);
    
    CPPUNIT_ASSERT(pirobs->erase((irob_id_t) 1) == true);
    CPPUNIT_ASSERT(get_pointer(pirobs->find(1)) == NULL);
    
    CPPUNIT_ASSERT(pirobs->empty() == true);

    PendingIROB *pirob42 = new PendingIROB(42, 0, NULL, 
                                           20, new char[20], 0);
    CPPUNIT_ASSERT(pirobs->insert(pirob42) == true);
    CPPUNIT_ASSERT(get_pointer(pirobs->find(42)) == pirob42);
    CPPUNIT_ASSERT(pirobs->erase((irob_id_t) 42) == true);
    CPPUNIT_ASSERT(get_pointer(pirobs->find(42)) == NULL);
}

void 
PendingIROBLatticeTest::testHoles()
{
    CPPUNIT_ASSERT(pirobs->insert(pirob0) == true);
    CPPUNIT_ASSERT(get_pointer(pirobs->find(0)) == pirob0);
    CPPUNIT_ASSERT(pirobs->insert(pirob1) == true);
    CPPUNIT_ASSERT(get_pointer(pirobs->find(1)) == pirob1);
    CPPUNIT_ASSERT(pirobs->insert(pirob2) == true);
    CPPUNIT_ASSERT(get_pointer(pirobs->find(2)) == pirob2);
    
    CPPUNIT_ASSERT(pirobs->erase((irob_id_t) 1) == true);
    CPPUNIT_ASSERT(get_pointer(pirobs->find(1)) == NULL);
    CPPUNIT_ASSERT(pirobs->erase((irob_id_t) 1) == false);
    
    CPPUNIT_ASSERT(pirobs->insert(pirob3) == true);
    CPPUNIT_ASSERT(get_pointer(pirobs->find(3)) == pirob3);
    CPPUNIT_ASSERT(pirobs->erase((irob_id_t) 3) == true);
    CPPUNIT_ASSERT(get_pointer(pirobs->find(3)) == NULL);
    
    CPPUNIT_ASSERT(pirobs->erase((irob_id_t) 0) == true);
    CPPUNIT_ASSERT(get_pointer(pirobs->find(0)) == NULL);
    CPPUNIT_ASSERT(get_pointer(pirobs->find(2)) == pirob2);
    
    CPPUNIT_ASSERT(pirobs->empty() == false);
    
    CPPUNIT_ASSERT(pirobs->erase((irob_id_t) 2) == true);
    CPPUNIT_ASSERT(get_pointer(pirobs->find(2)) == NULL);
    
    CPPUNIT_ASSERT(pirobs->empty() == true);
}

void
PendingIROBLatticeTest::testDependencies()
{
    CPPUNIT_ASSERT(pirobs->insert(pirob0) == true);
    CPPUNIT_ASSERT(get_pointer(pirobs->find(0)) == pirob0);
    CPPUNIT_ASSERT(pirobs->insert(pirob1) == true);
    CPPUNIT_ASSERT(get_pointer(pirobs->find(1)) == pirob1);
    CPPUNIT_ASSERT(pirobs->insert(pirob2) == true);
    CPPUNIT_ASSERT(get_pointer(pirobs->find(2)) == pirob2);

    CPPUNIT_ASSERT(pirob2->depends_on(1) == true);
    CPPUNIT_ASSERT(pirob1->depends_on(0) == true);
    CPPUNIT_ASSERT(pirob2->depends_on(0) == false);
    CPPUNIT_ASSERT(pirob2->depends_on(0) == false);
    CPPUNIT_ASSERT(pirob0->depends_on(1) == false);
    CPPUNIT_ASSERT(pirob0->depends_on(2) == false);
    CPPUNIT_ASSERT(pirob1->depends_on(2) == false);
    CPPUNIT_ASSERT(pirob0->depends_on(0) == false);
    CPPUNIT_ASSERT(pirob1->depends_on(1) == false);
    CPPUNIT_ASSERT(pirob2->depends_on(2) == false);
}
