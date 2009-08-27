#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>
#include "intset.h"

class IntSetTest : public CppUnit::TestFixture {
    IntSet *my_set;

    CPPUNIT_TEST_SUITE(IntSetTest);
    CPPUNIT_TEST(testInsert);
    CPPUNIT_TEST(testErase);
    CPPUNIT_TEST_SUITE_END();    

  public:
    void setUp();
    void tearDown();

    void testInsert();
    void testErase();
};
