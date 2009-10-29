#ifndef end_to_end_tests_base_h_incl
#define end_to_end_tests_base_h_incl

#include <cppunit/TestFixture.h>
#include <libcmm.h>

class EndToEndTestsBase : public CppUnit::TestFixture {
    static bool static_inited;
    static int listen_sock;
    static char *hostname;

  public:
    void setUp();
    void tearDown();

  protected:
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

    void testRandomBytesReceivedCorrectly();
    void testNoInterleaving();
    void testCMMPoll();
  private:
    void setupReceiver();
    void startReceiver();
    void startSender();

    void receiveAndChecksum();
    void sendChecksum(unsigned char *bytes, size_t size);
    void sendMessageSize(int size);
};

void handle_error(bool condition, const char *msg);

#endif
