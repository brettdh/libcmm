#ifndef spotty_network_failure_test_h_incl
#define spotty_network_failure_test_h_incl

#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>
#include "end_to_end_tests_remote.h"
#include <pthread.h>
#include <netinet/in.h>
#include <set>

/*
Failure scenario:

1) I have a CSocket connected on a network that's about to go down.
2) At time 1, the CSocket notices that its connection is dead.
   It closes its socket and its sender/receiver threads exit.
3) At time 2, the multisocket wants to send some data.  The sender
   thread for the always-available network picks up the IROB
   notification, but thinks the about-to-disappear network is still
   there, because the scout hasn't told it otherwise yet.  Since that
   network is better than its own, the thread calls
   new_csock_with_labels, which tries to start up a new CSocket and
   its threads.  This fails, on the new sender's thread.
4) However, the thread that tried to create the new CSocket never
   does anything after that fails.  The scout informs the library
   that the transient network is gone, and it re-inserts the IROB
   scheduling datum, but it never gets acted upon.


Proposed test setup:

1) Client running on an Android phone with fake connection scout
   connects to entirely fake server.

   Setup:
   a) Server listens on a given port.
   b) Client connects to port, sends HELLO + ifaces.
   c) Server accepts connection from port.
   d) Server opens new listening socket for fake multisocket connections.
   e) Server receives HELLO + ifaces, sends ifaces back along with 
      listener port (in HELLO response).
   
2) Both wait 1 second upon bootstrap completion.
3) Server closes its wifi-bound socket.
4) Client waits one second, then tries to send something FG.
   Meanwhile, client-side fake scout waits two seconds before
   telling app that the network is down.
5) The failure should now manifest.
*/

class SpottyNetworkFailureTest :  public EndToEndTestsRemote {
    CPPUNIT_TEST_SUITE(SpottyNetworkFailureTest);
    CPPUNIT_TEST(testOneNetworkFails);
    CPPUNIT_TEST_SUITE_END();

  public:
    virtual void tearDown();
    void testOneNetworkFails();

  protected:
    virtual void setupReceiver();

    static const in_port_t PROXY_PORT;
    static const in_port_t INTNW_LISTEN_PORT;

  private:
    bool processBootstrap(int to_sock, char *chunk, size_t len);
    bool processData(int to_sock, char *chunk, size_t len);

    typedef bool
        (SpottyNetworkFailureTest::*chunk_proc_method_t)(int, char *, size_t);
    
    friend bool process_chunk(int to_sock, char *chunk, size_t len,
                              SpottyNetworkFailureTest *test,
                              chunk_proc_method_t processMethod);
    friend bool process_bootstrap(int to_sock, char *chunk, size_t len, 
                                  SpottyNetworkFailureTest *test);
    friend bool process_data(int to_sock, char *chunk, size_t len, 
                             SpottyNetworkFailureTest *test);

    pthread_t bootstrap_proxy_thread;
    pthread_t internal_data_proxy_thread;
    bool bootstrap_done;

    pthread_mutex_t proxy_threads_lock;
    std::set<pthread_t> proxy_threads;
    u_long fg_socket_rtt;
    pthread_t fg_proxy_thread;
};

#endif
