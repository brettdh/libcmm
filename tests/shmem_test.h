#ifndef shmem_test_h_incl
#define shmem_test_h_incl

#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>

class ShmemTest : public CppUnit::TestFixture {
    CPPUNIT_TEST_SUITE(ShmemTest);
    CPPUNIT_TEST(testCount);
    CPPUNIT_TEST(testMap);

    // removed because procs can only increment once.
    //CPPUNIT_TEST(testRace);
    CPPUNIT_TEST_SUITE_END();    

    int master_fd;

  public:
    void setUp();
    void tearDown();

    void testCount();
    void testRace();
    void testMap();
};

#endif
