#include <cppunit/Test.h>
#include <cppunit/TestAssert.h>
#include <cppunit/TestSuite.h>
#include <cppunit/TestCaller.h>
#include "intset_test.h"

CppUnit::Test *
IntSetTest::suite()
{
    CppUnit::TestSuite *testSuite = new CppUnit::TestSuite("IntSet_Test");
    testSuite->addTest(new CppUnit::TestCaller<IntSetTest>(
                           "testInsert", 
                           &IntSetTest::testInsert));
    testSuite->addTest(new CppUnit::TestCaller<IntSetTest>(
                           "testErase", 
                           &IntSetTest::testErase));

    return testSuite;
}

void 
IntSetTest::setUp()
{
    my_set = new IntSet;
}

void 
IntSetTest::tearDown()
{
    delete my_set;
}

void 
IntSetTest::testInsert()
{
    for (int i = 0; i < 200; i++) {
        CPPUNIT_ASSERT(my_set->contains(i) == false);
        my_set->insert(i);
        CPPUNIT_ASSERT(my_set->contains(i));
    }
}

void 
IntSetTest::testErase()
{
    testInsert();
    for (int i = 0; i < 200; i++) {
        CPPUNIT_ASSERT(my_set->contains(i));
        my_set->erase(i);
        CPPUNIT_ASSERT(my_set->contains(i) == false);
    }
    testInsert();
}
