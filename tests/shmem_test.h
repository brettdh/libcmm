#ifndef shmem_test_h_incl
#define shmem_test_h_incl

#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>

class ShmemTest : public CppUnit::TestFixture {
    CPPUNIT_TEST_SUITE(ShmemTest);
    CPPUNIT_TEST(testGlobalBufferCount);
    CPPUNIT_TEST(testMap);
    CPPUNIT_TEST_SUITE_END();

  public:
    void testGlobalBufferCount();
    void testMap();
};

#endif
