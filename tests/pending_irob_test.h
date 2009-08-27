#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>
#include "pending_irob.h"

class PendingIROBLatticeTest : public CppUnit::TestFixture {
    PendingIROBLattice *pirobs;
    PendingIROB *pirob0;
    PendingIROB *pirob1;
    PendingIROB *pirob2;
    PendingIROB *pirob3;

    CPPUNIT_TEST_SUITE(PendingIROBLatticeTest);
    CPPUNIT_TEST(testInsert);
    CPPUNIT_TEST(testErase);
    CPPUNIT_TEST(testHoles);
    CPPUNIT_TEST(testDependencies);
    CPPUNIT_TEST_SUITE_END();

  public:
    void setUp();
    void tearDown();

    void testInsert();
    void testErase();
    void testHoles();
    void testDependencies();
};
