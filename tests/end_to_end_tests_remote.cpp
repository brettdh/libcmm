#include <cppunit/extensions/HelperMacros.h>
#include "end_to_end_tests_remote.h"

bool EndToEndTestsRemote::is_receiver = false;

extern bool g_receiver;
extern char *g_hostname;

void
EndToEndTestsRemote::chooseRole()
{
    // from command line, in run_all_tests.cpp
    is_receiver = g_receiver;
    setRemoteHost(g_hostname);
}

bool
EndToEndTestsRemote::isReceiver()
{
    return is_receiver;
}
