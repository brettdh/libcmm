#ifndef end_to_end_tests_base_h_incl
#define end_to_end_tests_base_h_incl

#include <cppunit/TestFixture.h>
#include <libcmm.h>

class EndToEndTestsBase :  public CppUnit::TestFixture {
  public:
    void setUp();
    void tearDown();

    void testRandomBytesReceivedCorrectly();
    void testNoInterleaving();
    void testPartialRecv();
    void testCMMPoll();
    void testHalfShutdown();

  protected:
    friend class NonBlockingTestsBase;

    // Should only be called once, in chooseRole().
    void setRemoteHost(const char *hostname_);

    // subclass should define state needed for isReceiver in chooseRole,
    //  and it should also call setRemoteHost.
    virtual void chooseRole() = 0;
    virtual bool isReceiver() = 0;
    virtual void waitForReceiver() {}

    void receiverAssertIntsSorted(int nums[], size_t n);

    mc_socket_t read_sock;
    mc_socket_t send_sock;

    virtual void setupReceiver();
    virtual void startReceiver();
    virtual void startSender();

    void receiveAndChecksum();
    void sendChecksum(unsigned char *bytes, size_t size);
    void sendMessageSize(int size);

    static bool static_inited;
    static int listen_sock;
    static char *hostname;
    static const short TEST_PORT;
};

#endif
