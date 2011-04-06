#ifndef socket_api_tests_h_incl
#define socket_api_tests_h_incl

#include "end_to_end_tests_remote.h"
#include <cppunit/extensions/HelperMacros.h>

class SocketAPITest : public EndToEndTestsRemote {
    CPPUNIT_TEST_SUITE(SocketAPITest);
    CPPUNIT_TEST(testBuffers);
    CPPUNIT_TEST_SUITE_END();  

    void setOpt(int sock, int opt);
    void checkOpt(int sock, int opt);

  protected:
    virtual void socketSetup();

  public:
    void testBuffers();
};

#endif
