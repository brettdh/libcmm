#include <cppunit/extensions/TestFactoryRegistry.h>
#include <cppunit/ui/text/TestRunner.h>

using CppUnit::TestFactoryRegistry;

int main()
{
    TestFactoryRegistry& registry = TestFactoryRegistry::getRegistry();
    CppUnit::TextUi::TestRunner runner;
    runner.addTest(registry.makeTest());
    
    runner.run();
    return 0;
}
