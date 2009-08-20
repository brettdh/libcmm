#include <cppunit/ui/text/TestRunner.h>
#include "pending_irob_test.h"
#include "lattice_test.h"
#include "receiver_lattice_test.h"

int main()
{
    CppUnit::TextUi::TestRunner runner;
    runner.addTest(PendingIROBLatticeTest::suite());
    runner.addTest(LatticeTest::suite());
    runner.addTest(ReceiverLatticeTest::suite());
    
    runner.run();
    return 0;
}
