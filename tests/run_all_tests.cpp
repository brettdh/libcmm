#include <cppunit/extensions/TestFactoryRegistry.h>
#include <cppunit/ui/text/TestRunner.h>
#include "StdioOutputter.h"
#include <unistd.h>

using CppUnit::TestFactoryRegistry;

bool g_receiver = false;
char *g_hostname = (char*)"localhost";

static void run_all_tests()
{
    TestFactoryRegistry& registry = TestFactoryRegistry::getRegistry();
    CppUnit::TextUi::TestRunner runner;
    runner.addTest(registry.makeTest());
    runner.setOutputter(new StdioOutputter(&runner.result()));
    runner.run();
}

int main(int argc, char *argv[])
{
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
    
    run_all_tests();
    return 0;
}
