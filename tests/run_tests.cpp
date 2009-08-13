#include <cppunit/ui/text/TestRunner.h>
#include "pending_irob_test.h"

int main()
{
    CppUnit::TextUi::TestRunner runner;
    runner.addTest(PendingIROBLatticeTest::suite());
    
    runner.run();
    return 0;
}
