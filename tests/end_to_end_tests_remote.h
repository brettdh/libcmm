#ifndef end_to_end_tests_remote_h_incl
#define end_to_end_tests_remote_h_incl

#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>
#include "end_to_end_tests_base.h"

class EndToEndTestsRemote : public EndToEndTestsBase {
    static bool is_receiver;
  
  protected:
    virtual void chooseRole();
    virtual bool isReceiver();
    
    void testDefaultIROBOrdering();
};

#endif
