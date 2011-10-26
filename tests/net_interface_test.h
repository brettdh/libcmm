#ifndef net_interface_test_h_incl
#define net_interface_test_h_incl

#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>
#include "net_interface.h"

class NetInterfaceTest : public CppUnit::TestFixture {
    CPPUNIT_TEST_SUITE(NetInterfaceTest);
    CPPUNIT_TEST(testNetworkFitsRestriction);
    CPPUNIT_TEST_SUITE_END();    

  public:
    void testNetworkFitsRestriction();
};

#endif
