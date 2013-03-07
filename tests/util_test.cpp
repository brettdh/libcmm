#include <cppunit/Test.h>
#include <cppunit/TestAssert.h>
#include "util_test.h"
#include "common.h"

#include <string>
using std::string;

#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>

CPPUNIT_TEST_SUITE_REGISTRATION(UtilTest);

void
UtilTest::testStringifyIP()
{
    const char *addrs[] = {
        "127.0.0.1", "0.0.0.0", "141.212.113.120"
    };
    size_t num_addrs = sizeof(addrs) / sizeof(const char *);
    
    for (size_t i = 0; i < num_addrs; ++i) {
        struct in_addr addr;
        inet_aton(addrs[i], &addr);

        StringifyIP s(&addr);
        CPPUNIT_ASSERT_EQUAL(string(addrs[i]), string(s.c_str()));
    }
}

void
UtilTest::testStringifyIPMultipleCalls()
{
    const char *addrs[] = {"141.212.113.120", "141.212.110.132"};
    char expected[64];
    char actual[64];

    snprintf(expected, 64, "%s %s", addrs[0], addrs[1]);

    struct in_addr addr0, addr1;
    inet_aton(addrs[0], &addr0);
    inet_aton(addrs[1], &addr1);

    // different calls should have different buffers
    snprintf(actual, 64, "%s %s",
             StringifyIP(&addr0).c_str(),
             StringifyIP(&addr1).c_str());
    // also, the C++ standard specifies that the temporary object exists
    //  until the end of the outermost expression,
    //  so there's no dangling pointer here.

    CPPUNIT_ASSERT_EQUAL(string(expected), string(actual));
}
