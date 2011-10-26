#include <cppunit/Test.h>
#include <cppunit/TestAssert.h>
#include "net_interface_test.h"
#include "net_interface.h"

#include "libcmm_net_restriction.h"

#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>

CPPUNIT_TEST_SUITE_REGISTRATION(NetInterfaceTest);

void
NetInterfaceTest::testNetworkFitsRestriction()
{
    struct net_interface wifi, threeg, server;

    memset(&wifi, 0, sizeof(wifi));
    memset(&threeg, 0, sizeof(threeg));
    memset(&server, 0, sizeof(server));

    wifi.type = NET_TYPE_WIFI;
    threeg.type = NET_TYPE_THREEG;
    
    inet_aton("1.2.3.4", &wifi.ip_addr);
    inet_aton("5.6.7.8", &threeg.ip_addr);
    inet_aton("9.9.9.9", &server.ip_addr);

    CPPUNIT_ASSERT(network_fits_restriction(CMM_LABEL_WIFI_ONLY, wifi, server));
    CPPUNIT_ASSERT(network_fits_restriction(CMM_LABEL_WIFI_ONLY, server, wifi));
    CPPUNIT_ASSERT(!network_fits_restriction(CMM_LABEL_WIFI_ONLY, threeg, server));
    CPPUNIT_ASSERT(!network_fits_restriction(CMM_LABEL_WIFI_ONLY, server, threeg));

    CPPUNIT_ASSERT(network_fits_restriction(CMM_LABEL_THREEG_ONLY, threeg, server));
    CPPUNIT_ASSERT(network_fits_restriction(CMM_LABEL_THREEG_ONLY, server, threeg));
    CPPUNIT_ASSERT(!network_fits_restriction(CMM_LABEL_THREEG_ONLY, wifi, server));
    CPPUNIT_ASSERT(!network_fits_restriction(CMM_LABEL_THREEG_ONLY, server, wifi));
}
