#include <cppunit/TestFixture.h>
#include <libcmm.h>

class DataIntegrityTest : public CppUnit::TestFixture {
    pid_t scout_pid;
    pid_t receiver_pid; // 0 if it's me

    static int listen_sock;
    mc_socket_t read_sock;

    mc_socket_t send_sock;

    CPPUNIT_TEST_SUITE(DataIntegrityTest);
    CPPUNIT_TEST(testRandomBytesReceivedCorrectly);
    CPPUNIT_TEST(testOrderingSimple);
    CPPUNIT_TEST_SUITE_END();

  public:
    void setUp();
    void tearDown();

  protected:
    void startReceiver();
    void startSender();

    void receiverAssertIntsSorted(int nums[], size_t n);

    void testRandomBytesReceivedCorrectly();
    void testOrderingSimple();
};
