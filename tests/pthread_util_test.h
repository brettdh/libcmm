#ifndef intset_test_h_incl
#define intset_test_h_incl

#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>
#include "pthread_util.h"

class PthreadUtilTest : public CppUnit::TestFixture {
    CPPUNIT_TEST_SUITE(PthreadUtilTest);
    CPPUNIT_TEST(testQueue);
    CPPUNIT_TEST_SUITE_END();    

  public:
    void setUp();
    void tearDown();

    void testQueue();
};

#endif
