#ifndef socket_api_tests_h_incl
#define socket_api_tests_h_incl

#include "end_to_end_tests_forked.h"
#include <cppunit/extensions/HelperMacros.h>

class SocketAPITest : public EndToEndTestsForked {
    CPPUNIT_TEST_SUITE(SocketAPITest);
    //CPPUNIT_TEST(testBuffers);
    CPPUNIT_TEST(testLabelsReturnedOnIROBBoundaries);
    CPPUNIT_TEST(testDroppedIROBFailureCases);
    CPPUNIT_TEST(testPeek);
    CPPUNIT_TEST(testReceiveTimeout);
    CPPUNIT_TEST(testDroppedIROBWithDefaultIROBs);
    CPPUNIT_TEST(testSelect);
    CPPUNIT_TEST_SUITE_END();  

    void setOpt(int sock, int opt);
    void checkOpt(int sock, int opt);
    
    void setReceiveTimeout(int seconds);

  protected:
    virtual void socketSetup();

  public:
    void testBuffers();
    void testLabelsReturnedOnIROBBoundaries();
    void testDroppedIROBFailureCases();
    void testPeek();
    void testReceiveTimeout();
    void testDroppedIROBWithDefaultIROBs();
    void testSelect();
};

#endif
