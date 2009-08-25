#include <cppunit/TestFixture.h>
#include "intset.h"

class IntSetTest : public CppUnit::TestFixture {
    IntSet *my_set;
  public:
    static CppUnit::Test *suite();

    void setUp();
    void tearDown();

    void testInsert();
    void testErase();
};
