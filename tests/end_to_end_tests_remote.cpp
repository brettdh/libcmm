#include <cppunit/extensions/HelperMacros.h>
#include "end_to_end_tests_remote.h"
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <openssl/sha.h>
#include <assert.h>
#include <libcmm.h>
#include <libcmm_irob.h>

CPPUNIT_TEST_SUITE_REGISTRATION(EndToEndTestsRemote);

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

