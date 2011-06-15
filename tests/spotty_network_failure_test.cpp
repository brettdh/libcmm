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
#include <dlfcn.h>
#include <libcmm.h>
#include <libcmm_irob.h>
#include "net_interface.h"
#include "test_common.h"
#include "cmm_socket_control.h"

CPPUNIT_TEST_SUITE_REGISTRATION(SpottyNetworkFailureTest);

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
SpottyNetworkFailureTest::startReceiver()
{
    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);

    int bootstrap_sock = accept(listen_sock, 
                                (struct sockaddr *)&addr,
                                &addrlen);
    handle_error(bootstrap_sock < 0, "accept");
    DEBUG_LOG("Receiver accepted connection %d\n", bootstrap_sock);

    doFakeIntNWSetup(bootstrap_sock);
    close(bootstrap_sock);
}

void
SpottyNetworkFailureTest::doFakeIntNWSetup(int bootstrap_sock)
{
    struct CMMSocketControlHdr hello;
    int rc = read(bootstrap_sock, &hello, sizeof(hello));
    handle_error(rc != sizeof(hello), "receiving intnw hello");

    short intnw_listen_port = 42429;
    intnw_listen_sock = make_listening_socket(intnw_listen_port);
    handle_error(intnw_listen_sock < 0, "creating intnw listener socket");
    
    hello.op.hello.listen_port = htons(intnw_listen_port);
    hello.op.hello.num_ifaces = htonl(1);
    rc = write(bootstrap_sock, &hello, sizeof(hello));
    handle_error(rc != sizeof(hello), "sending intnw hello");

    acceptCsocks();
    exchangeNetworkInterfaces(bootstrap_sock);
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
SpottyNetworkFailureTest::acceptCsocks()
{
    struct net_interface steady_iface, intermittent_iface;

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
SpottyNetworkFailureTest::exchangeNetworkInterfaces(int bootstrap_sock)
{
    struct net_interface sentinel;
    memset(&sentinel, 0, sizeof(sentinel));
    int rc;
    struct CMMSocketControlHdr hdr;
    do {
        rc = read(bootstrap_sock, &hdr, sizeof(hdr));
        handle_error(rc != sizeof(hdr), "reading net iface");
    } while (memcmp(&hdr.op.new_interface, &sentinel, sizeof(struct net_interface)) != 0);

    struct CMMSocketControlHdr hdrs[2];
    memset(hdrs, 0, sizeof(hdrs));
    hdrs[0].type = htons(CMM_CONTROL_MSG_NEW_INTERFACE);
    inet_aton("141.212.110.132", &hdrs[0].op.new_interface.ip_addr);
    hdrs[0].op.new_interface.bandwidth_down = 1250000;
    hdrs[0].op.new_interface.bandwidth_up = 1250000;
    hdrs[0].op.new_interface.RTT = 1;
    hdrs[1].type = htons(CMM_CONTROL_MSG_NEW_INTERFACE);
    hdrs[0].op.new_interface = sentinel;
    rc = write(bootstrap_sock, hdrs, sizeof(hdrs));
    handle_error(rc != sizeof(hdrs), "sending net iface");
}

void
SpottyNetworkFailureTest::startSender()
{
    // start up intnw
    void *dl_handle = dlopen("libcmm.so", RTLD_LAZY);
    handle_error(dl_handle == NULL, "loading libcmm: dlopen");
    
    // create connecting multisocket
    EndToEndTestsBase::startSender();
}

void
SpottyNetworkFailureTest::tearDown()
{
    if (isReceiver()) {
        close(steady_csock);
        close(intermittent_csock);
        close(intnw_listen_sock);
        close(listen_sock);
    } else {
        EndToEndTestsBase::tearDown();
    }
}

int
connect_to_scout_control()
{
    int sock = socket(PF_INET, SOCK_STREAM, 0);
    handle_error(sock < 0, "creating scout control socket");

    struct sockaddr addr;
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
    const size_t len = sizeof(expected_str);
    sleep(1);

    if (isReceiver()) {
        char buf[len + 1];
        memset(buf, 0, sizeof(buf));

        shutdown(intermittent_csock, SHUT_RDWR);

        // expected messages:
        //  in order:
        //  1) begin_irob
        //  2) irob_chunk
        //  3) end_irob
        //
        // at any time:
        //  - down_interface
        vector<struct CMMSocketControlHdr> hdrs;
        while (hdrs.size() < 4) {
            struct CMMSocketControlHdr hdr;
            memset(&hdr, 0, sizeof(hdr));
            int rc = recv(steady_csock, &hdr, sizeof(hdr), MSG_WAITALL);
            CPPUNIT_ASSERT_EQUAL((int)sizeof(hdr), rc);
            if (ntohs(hdr.type) == CMM_CONTROL_MSG_IROB_CHUNK) {
                CPPUNIT_ASSERT_EQUAL(len, ntohl(hdr.op.irob_chunk.datalen));
                
                rc = recv(steady_csock, buf, len, MSG_WAITALL);
                CPPUNIT_ASSERT_EQUAL(rc, len);
                CPPUNIT_ASSERT_EQUAL(string(expected_str), string(buf));
            }
            
            hdrs.push_back(hdr);
        }
        CPPUNIT_ASSERT_EQUAL(4, hdrs.size());

        irob_id_t irob_id = -1;
        bool begin_irob_recvd = false;
        bool irob_chunk_recvd = false;
        bool end_irob_recvd = false;
        bool down_interface_recvd = false;
        for (size_t i = 0; i < hdrs.size(); ++i) {
            switch (ntohs(hdr.type)) {
            case CMM_CONTROL_MSG_BEGIN_IROB:
                irob_id = ntohl(hdrs.op.begin_irob.id);
                begin_irob_recvd = true; break;
            case CMM_CONTROL_MSG_IROB_CHUNK:
                irob_chunk_recvd = true; break;
            case CMM_CONTROL_MSG_END_IROB:
                end_irob_recvd = true; break;
            case CMM_CONTROL_MSG_DOWN_INTERFACE:
                down_interface_recvd = true; break;
            default:
                CPPUNIT_ASSERT(false);
            }
        }
        CPPUNIT_ASSERT(begin_irob_recvd);
        CPPUNIT_ASSERT(irob_chunk_recvd);
        CPPUNIT_ASSERT(end_irob_recvd);
        CPPUNIT_ASSERT(down_interface_recvd);

        struct CMMSocketControlHdr ack;
        memset(&ack, 0, sizeof(ack));
        ack.type = htons(CMM_CONTROL_MSG_ACK);
        ack.op.id = htonl(irob_id);
        rc = write(steady_csock, &ack, sizeof(ack));
        CPPUNIT_ASSERT_EQUAL((int)sizeof(ack), rc);

        char response[sizeof(struct CMMSocketControlHdr) * 3 + len];
        memset(response, 0, sizeof(response));
        struct CMMSocketControlHdr *begin_irob_hdr = response;
        struct CMMSocketControlHdr *irob_chunk_hdr = ((char *) begin_irob_hdr) + sizeof(*begin_irob_hdr);
        char *resp_data = ((char *) irob_chunk_hdr) + sizeof(*irob_chunk_hdr);
        struct CMMSocketControlHdr *end_irob_hdr = 
            (struct CMMSocketControlHdr *) (resp_data + len);

        irob_id_t resp_irob_id = irob_id + 1;
        begin_irob_hdr->type = htons(CMM_CONTROL_MSG_BEGIN_IROB);
        begin_irob_hdr->op.begin_irob.id = htonl(resp_irob_id);
        irob_chunk_hdr->op.irob_chunk.id = htonl(resp_irob_id);
        irob_chunk_hdr->op.irob_chunk.seqno = 0;
        irob_chunk_hdr->op.irob_chunk.datalen = htonl(len);
        memcpy(resp_data, buf, len);
        end_irob_hdr->op.end_irob.id = htonl(resp_irob_id);
        end_irob_hdr->op.end_irob.expected_bytes = htonl(len);
        end_irob_hdr->op.end_irob.expected_chunks = htonl(1);
        
        rc = write(steady_csock, response, sizeof(response));
        CPPUNIT_ASSERT_EQUAL((int) sizeof(response), rc);

        memset(&ack, 0, sizeof(ack));
        rc = recv(steady_csock, &ack, sizeof(ack), MSG_WAITALL);
        CPPUNIT_ASSERT_EQUAL((int) sizeof(ack), rc);
        CPPUNIT_ASSERT_EQUAL(CMM_CONTROL_MSG_ACK, ntohs(ack.type));
        CPPUNIT_ASSERT_EQUAL(resp_irob_id, ntohl(ack.op.ack_data.id));
    } else {
        int scout_control_sock = connect_to_scout_control();
        
        sleep(1);
        int rc = cmm_write(data_sock, buf, len);
        CPPUNIT_ASSERT_EQUAL(len, rc); // succeeds immediately without waiting for bytes to be sent
        
        sleep(1);
        char cmd[] = "bg_down\n";
        rc = write(scout_control_sock, cmd, sizeof(cmd));
        CPPUNIT_ASSERT_EQUAL((int) sizeof(cmd), rc);

        sleep(1);
        memset(buf, 0, sizeof(buf));
        rc = cmm_recv(data_sock, buf, len, MSG_WAITALL, NULL);
        CPPUNIT_ASSERT_EQUAL(len, rc);
        CPPUNIT_ASSERT_EQUAL(string(expected_bytes), string(buf));
        
        close(scout_control_sock);
    }
}
