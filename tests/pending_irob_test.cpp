#include <cppunit/Test.h>
#include <cppunit/TestAssert.h>
#include <cppunit/TestSuite.h>
#include <cppunit/TestCaller.h>
#include "pending_irob_test.h"

CppUnit::Test *
PendingIROBLatticeTest::suite()
{
    CppUnit::TestSuite *testSuite = new CppUnit::TestSuite("PendingIROBLattice_Test");
    testSuite->addTest(new CppUnit::TestCaller<PendingIROBLatticeTest>(
                           "testInsert", 
                           &PendingIROBLatticeTest::testInsert));
    testSuite->addTest(new CppUnit::TestCaller<PendingIROBLatticeTest>(
                           "testErase", 
                           &PendingIROBLatticeTest::testErase));
    testSuite->addTest(new CppUnit::TestCaller<PendingIROBLatticeTest>(
                           "testHoles", 
                           &PendingIROBLatticeTest::testHoles));
    testSuite->addTest(new CppUnit::TestCaller<PendingIROBLatticeTest>(
                           "testDependencies", 
                           &PendingIROBLatticeTest::testDependencies));
    return testSuite;
}

void 
PendingIROBLatticeTest::setUp()
{
    pirobs = new PendingIROBLattice();
    pirob0 = new PendingIROB(0, 20, new char[20], 0, 0);
    pirob1 = new PendingIROB(1, 20, new char[20], 0, 0);
    pirob2 = new PendingIROB(2, 20, new char[20], 0, 0);
    pirob3 = new PendingIROB(3, 20, new char[20], 0, 0);
}

void 
PendingIROBLatticeTest::tearDown()
{
    delete pirob0;
    delete pirob1;
    delete pirob2;
    delete pirob3;

    pirobs->clear();
    delete pirobs;
}

void 
PendingIROBLatticeTest::testInsert()
{
    CPPUNIT_ASSERT(pirobs->empty() == true);
    CPPUNIT_ASSERT(pirobs->insert(pirob0) == true);
    CPPUNIT_ASSERT(pirobs->find(0) == pirob0);
    CPPUNIT_ASSERT(pirobs->insert(pirob1) == true);
    CPPUNIT_ASSERT(pirobs->find(1) == pirob1);
    CPPUNIT_ASSERT(pirobs->insert(pirob1) == false);
    CPPUNIT_ASSERT(pirobs->empty() == false);
}

void 
PendingIROBLatticeTest::testErase()
{
    CPPUNIT_ASSERT(pirobs->empty() == true);
    CPPUNIT_ASSERT(pirobs->insert(pirob0) == true);
    CPPUNIT_ASSERT(pirobs->find(0) == pirob0);
    CPPUNIT_ASSERT(pirobs->insert(pirob1) == true);
    CPPUNIT_ASSERT(pirobs->find(1) == pirob1);
    
    CPPUNIT_ASSERT(pirobs->empty() == false);
    
    CPPUNIT_ASSERT(pirobs->erase(0) == true);
    CPPUNIT_ASSERT(pirobs->find(0) == NULL);
    CPPUNIT_ASSERT(pirobs->erase(0) == false);
    
    CPPUNIT_ASSERT(pirobs->find(1) == pirob1);
    CPPUNIT_ASSERT(pirobs->empty() == false);
    
    CPPUNIT_ASSERT(pirobs->erase(1) == true);
    CPPUNIT_ASSERT(pirobs->find(1) == NULL);
    
    CPPUNIT_ASSERT(pirobs->empty() == true);

    PendingIROB *pirob42 = new PendingIROB(42, 20, new char[20], 0, 0);
    CPPUNIT_ASSERT(pirobs->insert(pirob42) == true);
    CPPUNIT_ASSERT(pirobs->find(42) == pirob42);
    CPPUNIT_ASSERT(pirobs->erase(42) == true);
    CPPUNIT_ASSERT(pirobs->find(42) == NULL);
    delete pirob42;
}

void 
PendingIROBLatticeTest::testHoles()
{
    CPPUNIT_ASSERT(pirobs->insert(pirob0) == true);
    CPPUNIT_ASSERT(pirobs->find(0) == pirob0);
    CPPUNIT_ASSERT(pirobs->insert(pirob1) == true);
    CPPUNIT_ASSERT(pirobs->find(1) == pirob1);
    CPPUNIT_ASSERT(pirobs->insert(pirob2) == true);
    CPPUNIT_ASSERT(pirobs->find(2) == pirob2);
    
    CPPUNIT_ASSERT(pirobs->erase(1) == true);
    CPPUNIT_ASSERT(pirobs->find(1) == NULL);
    CPPUNIT_ASSERT(pirobs->erase(1) == false);
    
    CPPUNIT_ASSERT(pirobs->insert(pirob3) == true);
    CPPUNIT_ASSERT(pirobs->find(3) == pirob3);
    CPPUNIT_ASSERT(pirobs->erase(3) == true);
    CPPUNIT_ASSERT(pirobs->find(3) == NULL);
    
    CPPUNIT_ASSERT(pirobs->erase(0) == true);
    CPPUNIT_ASSERT(pirobs->find(0) == NULL);
    CPPUNIT_ASSERT(pirobs->find(2) == pirob2);
    
    CPPUNIT_ASSERT(pirobs->empty() == false);
    
    CPPUNIT_ASSERT(pirobs->erase(2) == true);
    CPPUNIT_ASSERT(pirobs->find(2) == NULL);
    
    CPPUNIT_ASSERT(pirobs->empty() == true);
}

void
PendingIROBLatticeTest::testDependencies()
{
    CPPUNIT_ASSERT(pirobs->insert(pirob0) == true);
    CPPUNIT_ASSERT(pirobs->find(0) == pirob0);
    CPPUNIT_ASSERT(pirobs->insert(pirob1) == true);
    CPPUNIT_ASSERT(pirobs->find(1) == pirob1);
    CPPUNIT_ASSERT(pirobs->insert(pirob2) == true);
    CPPUNIT_ASSERT(pirobs->find(2) == pirob2);

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
