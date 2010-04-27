#include <cppunit/extensions/TestFactoryRegistry.h>
#include <cppunit/ui/text/TestRunner.h>
#include <unistd.h>

using CppUnit::TestFactoryRegistry;

bool g_receiver = false;
char *g_hostname = (char*)"localhost";

int main(int argc, char *argv[])
{
    TestFactoryRegistry& registry = TestFactoryRegistry::getRegistry();
    CppUnit::TextUi::TestRunner runner;
    runner.addTest(registry.makeTest());
    
    int ch;
    while ((ch = getopt(argc, argv, "lh:")) != -1) {
        switch (ch) {
        case 'l':
            g_receiver = true;
            break;
        case 'h':
            g_hostname = optarg;
            break;
        case '?':
            exit(EXIT_FAILURE);
        default:
            break;
        }
    }
    
    runner.run();
    return 0;
}
