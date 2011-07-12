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
#include <string.h>
#include <libcmm.h>
#include <libcmm_irob.h>
#include "net_interface.h"
#include "test_common.h"
#include "cmm_socket_control.h"
#include "libcmm_ipc.h"

#include <string>
#include <vector>
using std::string; using std::vector;

CPPUNIT_TEST_SUITE_REGISTRATION(SpottyNetworkFailureTest);

/* In the process of revising this test as follows.
 *
 * What this test currently does:
 *  1) Fake a multisocket on the server end.
 *  2) Connect a real multisocket to the fake one.
 *  3) Shut down one of the TCP sockets.
 *  4) Try to send/receive data.
 *
 * The trouble with this approach is that the multisocket can
 *  detect the TCP connection being shut down and react to it.
 *  I really want to test what happens when the TCP connection
 *  just stops receiving any messages (including FIN, for example).
 *
 * What the revised test will do:
 *  1) Create a real multisocket on the server end.
 *  2) Create proxy sockets:
 *     a) Between the IntNW listen socket and the client
 *     b) Between the multisocket's internal listen socket 
 *        and the client's connecting csockets
 *  3) Rewrite the necessary messages to keep the multisocket
 *     endpoints unaware of the proxy:
 *     a) The listener port in the initial HELLO response
 *  4) Snoop on the csocket setup messages to determine which
 *     one should be considered FG
 *  5) Stop proxying data on that csocket.
 *  6) Try to send/receive FG data.
 */

short SpottyNetworkFailureTest::PROXY_PORT = 4243;
short SpottyNetworkFailureTest::INTNW_LISTEN_PORT = 42424;

static bool process_chunk(int to_sock, char *chunk, size_t len, 
                          SpottyNetworkFailureTest *test,
                          SpottyNetworkFailureTest::chunk_proc_method_t processMethod)
{
    return (test->*processMethod)(to_sock, chunk, len);
}

static bool process_bootstrap(int to_sock, char *chunk, size_t len, 
                              SpottyNetworkFailureTest *test)
{
    return process_chunk(to_sock, chunk, len, test, 
                         &SpottyNetworkFailureTest::processBootstrap);
}

static bool process_data(int to_sock, char *chunk, size_t len, 
                         SpottyNetworkFailureTest *test)
{
    return process_chunk(to_sock, chunk, len, test, 
                         &SpottyNetworkFailureTest::processData);
}

void
SpottyNetworkFailureTest::startReceiver()
{
    bootstrap_done = false;
    
    setListenPort(PROXY_PORT);
    start_proxy_thread(&bootstrap_proxy_thread, TEST_PORT, PROXY_PORT, 
                       (chunk_proc_fn_t) process_bootstrap, this);
    start_proxy_thread(&internal_data_proxy_thread, 
                       INTNW_LISTEN_PORT, INTNW_LISTEN_PORT + 1,
                       (chunk_proc_fn_t) process_data, this);

    EndToEndTestsBase::startReceiver();
}

bool
SpottyNetworkFailureTest::processBootstrap(int to_sock, char *chunk, size_t len)
{
    // overwrite internal-listener port with the proxy's port
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    socklen_t addrlen = sizeof(addr);
    if (!bootstrap_done &&
        getsockname(to_sock, (struct sockaddr *) &addr, &addrlen) == 0 &&
        ntohs(addr.sin_port) == TEST_PORT) {
        // only modify the hello response, not the request
        struct CMMSocketControlhdr *hdr = (void *) chunk;
        if (ntohs(hdr->type) == CMM_CONTROL_MSG_HELLO) {
            short listener_port = ntohs(hdr->op.hello.listen_port);
            printf("Overwriting listener port %d in hello response with proxy port %d\n",
                   listener_port, INTNW_LISTEN_PORT);
            hdr->op.hello.listen_port = htons(INTNW_LISTEN_PORT);
            bootstrap_done = true;
        }
    }

    return true;
}

bool
SpottyNetworkFailureTest::processData(int to_sock, char *chunk, size_t len)
{
    // TODO: sniff out the RTT info to decide whether the socket is FG
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    socklen_t addrlen = sizeof(addr);
    if (getsockname(to_sock, (struct sockaddr *) &addr, &addrlen) == 0 &&
        ntohs(addr.sin_port) == INTNW_LISTEN_PORT) {
        struct CMMSocketControlHdr *hdr = (void *) chunk;
        // XXX: make sure this is in fact the new_interface message for the new csocket
        // XXX:  (probably not strictly necessary, since that message
        // XXX:   is the first message on the csocket, but still
        // XXX:   a good sanity check.)
        if (ntohs(hdr->type) == CMM_CONTROL_MSG_NEW_INTERFACE) {
            if (ntohl(hdr->op.new_interface.RTT) < fg_socket_rtt) {
                fg_socket = to_sock;
                fg_proxy_thread = pthread_self();
            }
        }
    }

    // TODO: stop passing data after a certain time
    if (fg_proxy_thread == pthread_self() && 
        false /* TODO: actual pause condition */) {
        return false;
    }
    return true;
}


void
SpottyNetworkFailureTest::tearDown()
{
    if (isReceiver()) {
        EndToEndTestsBase::tearDown();
        // TODO: shutdown proxy threads
    } else {
        EndToEndTestsBase::tearDown();
    }
}

int
connect_to_scout_control()
{
    int sock = socket(PF_INET, SOCK_STREAM, 0);
    handle_error(sock < 0, "creating scout control socket");

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(CONTROL_SOCKET_PORT);

    socklen_t addrlen = sizeof(addr);
    int rc = connect(sock, (struct sockaddr *)&addr, addrlen);
    handle_error(rc < 0, "connecting scout control socket");
    
    return sock;
}

void 
SpottyNetworkFailureTest::testOneNetworkFails()
{
    const char expected_str[] = "ABCDEFGHIJ";
    const size_t len = strlen(expected_str);

    char buf[len + 1];
    memset(buf, 0, sizeof(buf));

    if (isReceiver()) {
        char resp_data[len + 1];
        int rc = cmm_read(data_sock, resp_data, len, NULL);
        CPPUNIT_ASSERT_EQUAL((int)len, rc);
        resp_data[rc] = '\0';

        rc = cmm_write(data_sock, expected_str, len);
        CPPUNIT_ASSERT_EQUAL((int) sizeof(response), rc);
    } else {
        int scout_control_sock = connect_to_scout_control();
        
        sleep(1);
        int rc = cmm_write(data_sock, expected_str, len,
                           CMM_LABEL_ONDEMAND, NULL, NULL);
        CPPUNIT_ASSERT_EQUAL((int)len, rc); // succeeds immediately without waiting for bytes to be sent
        
        sleep(5);
        char cmd[] = "bg_down\n";
        rc = write(scout_control_sock, cmd, strlen(cmd));
        CPPUNIT_ASSERT_EQUAL((int) strlen(cmd), rc);

        sleep(1);
        memset(buf, 0, sizeof(buf));
        rc = cmm_recv(data_sock, buf, len, MSG_WAITALL, NULL);
        CPPUNIT_ASSERT_EQUAL((int) len, rc);
        CPPUNIT_ASSERT_EQUAL(string(expected_str), string(buf));
        
        close(scout_control_sock);
    }
}
