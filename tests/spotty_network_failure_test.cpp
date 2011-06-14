#include <cppunit/extensions/HelperMacros.h>
#include "spotty_network_failure_test.h"
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
#include <assert.h>
#include <libcmm.h>
#include <libcmm_irob.h>

CPPUNIT_TEST_SUITE_REGISTRATION(SpottyNetworkFailureTests);

int
make_listening_socket(short port)
{
    int sock = socket(PF_INET, SOCK_STREAM, 0);
    handle_error(sock < 0, "socket");
    
    int on = 1;
    int rc = setsockopt(sock, SOL_SOCKET, SO_REUSEADDR,
                        (char *) &on, sizeof(on));
    if (rc < 0) {
        DEBUG_LOG("Cannot reuse socket address\n");
    }
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    
    socklen_t addrlen = sizeof(addr);
    rc = bind(sock, (struct sockaddr*)&addr, addrlen);
    handle_error(rc < 0, "bind");
    
    rc = listen(sock, 5);
    handle_error(rc < 0, "cmm_listen");
    DEBUG_LOG("Receiver is listening...\n");
    
    return sock;
}

void
SpottyNetworkFailureTest::setupReceiver()
{
    listen_sock = make_listening_socket(TEST_PORT);
}

void
SpottyNetworkFailureTests::startReceiver()
{
    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);

    bootstrap_sock = accept(listen_sock, 
                            (struct sockaddr *)&addr,
                            &addrlen);
    handle_error(bootstrap_sock < 0, "accept");
    DEBUG_LOG("Receiver accepted connection %d\n", bootstrap_sock);

    doFakeIntNWSetup();
}

void
SpottyNetworkFailureTests::doFakeIntNWSetup()
{
    struct CMMSocketControlHdr hello;
    int rc = read(bootstrap_sock, &hello, sizeof(hello));
    handle_error(rc != sizeof(hello), "receiving intnw hello");

    short intnw_listen_port = 42429;
    intnw_listen_sock = make_listening_socket(intnw_listen_port);
    handle_error(intnw_listen_sock < 0, "creating intnw listener socket");
    
    hello.listen_port = htons(intnw_listen_port);
    hello.num_ifaces = htonl(1);
    rc = write(bootstrap_sock, &hello, sizeof(hello));
    handle_error(rc != sizeof(hello), "sending intnw hello");

    acceptCsocks();
    
    // TODO: receive and send network interfaces.
    struct net_interface sentinel = {0};
}

struct net_interface init_csocket(int csock)
{
    struct CMMSocketControlHdr hdr;
    struct net_interface iface;
    int rc = read(csock, &hdr, sizeof(hdr));
    handle_error(rc != sizeof(hdr), "reading csock net_interface data");
    assert(ntohs(hdr.type) == CMM_CONTROL_MSG_NEW_INTERFACE);

    iface = hdr.op.new_interface;
    iface.bandwidth_down = ntohl(iface.bandwidth_down);
    iface.bandwidth_up = ntohl(iface.bandwidth_up);
    iface.RTT = ntohl(iface.RTT);

    memset(&hdr, 0, sizeof(hdr));
    hdr.type = htons(CMM_CONTROL_MSG_HELLO);
    rc = write(csock, &hdr, sizeof(hdr));
    handle_error(rc != sizeof(hdr), "sending csock confirmation");
    
    return iface;
}

void
SpottyNetworkFailureTests::acceptCsocks()
{
    struct net_interface steady_iface, intermitent_iface;

    steady_csock = accept(intnw_listen_sock, NULL, NULL);
    handle_error(steady_csock < 0, "accepting csocket");
    
    steady_iface = init_csocket(steady_csock);

    intermittent_csock = accept(intnw_listen_sock, NULL, NULL);
    handle_error(steady_csock < 0, "accepting csocket");
    
    intermittent_iface = init_csocket(intermittent_csock);

    // the better network is the intermittent one.
    if (steady_iface.RTT < intermittent_iface.RTT) {
        int tmpsock = steady_csock;
        steady_csock = intermittent_csock;
        intermittent_csock = tmpsock;
    }
}

void
SpottyNetworkFailureTests::startSender()
{
    // start fake scout
    scout_stdin = popen("conn_scout stdin rmnet0 12500 12500 100 tiwlan0 125000 125000 1", "w");
    handle_error(scout_stdin == NULL, "starting scout: popen");

    // start up intnw
    dl_handle = dlopen("libcmm.so", RTLD_LAZY);
    handle_error(dl_handle == NULL, "loading libcmm: dlopen");
    
    // create connecting multisocket
    EndToEndTestsBase::startSender();
}

void
SpottyNetworkFailureTests::tearDown()
{
    EndToEndTestsBase::tearDown();
    
    pclose(scout_stdin);
}

void 
SpottyNetworkFailureTests::testOneNetworkFails()
{
    
}
