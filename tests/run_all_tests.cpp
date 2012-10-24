#include <cppunit/extensions/TestFactoryRegistry.h>
#include <cppunit/ui/text/TestRunner.h>
#include "StdioOutputter.h"
#include <unistd.h>
#include <stdlib.h>
#include <iostream>
#include <stdexcept>
using std::cout; using std::endl;
using std::exception;

using CppUnit::TestFactoryRegistry;

#include "test_common.h"
#include "libcmm.h"

bool g_receiver = false;
char *g_hostname = (char*)"localhost";
#ifndef CMM_UNIT_TESTING
int g_network_strategy = INTNW_NEVER_REDUNDANT;
#endif

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
    while ((ch = getopt(argc, argv, "lh:s:")) != -1) {
        switch (ch) {
        case 'l':
            g_receiver = true;
            break;
        case 'h':
            g_hostname = optarg;
            break;
        case 's':
#ifndef CMM_UNIT_TESTING
            g_network_strategy = get_redundancy_strategy_type(optarg);
            if (g_network_strategy == -1) {
                DEBUG_LOG("Unrecognized network strategy: %s\n", optarg);
                exit(1);
            }
#endif
            break;
        case '?':
            exit(EXIT_FAILURE);
        default:
            break;
        }
    }
    
    try {
        run_all_tests();
    } catch (exception& e) {
        cout << e.what() << endl;
        return 1;
    }
    return 0;
}
