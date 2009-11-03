#ifndef end_to_end_tests_forked_h_incl
#define end_to_end_tests_forked_h_incl

#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>
#include "end_to_end_tests_base.h"
#include <sys/wait.h>

class EndToEndTestsForked : public EndToEndTestsBase {
    static pid_t receiver_pid; // 0 if it's me

  public:
    //void testRandomBytesReceivedCorrectly();
    void testOrderingSimple();
    void testOrderingReverse();

  protected:
    virtual void chooseRole();
    virtual bool isReceiver();
    virtual void waitForReceiver();

    class static_destroyer {
      public:
        ~static_destroyer() {
            if (receiver_pid != 0) {
                printf("Waiting for receiver to die\n");
                pid_t rc = waitpid(receiver_pid, NULL, 0);
                printf("Receiver died, rc = %d\n", rc);
            }
        }
    };
    static static_destroyer destroyer;
};

#endif
