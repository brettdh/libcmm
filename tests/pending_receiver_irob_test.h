#ifndef pending_receiver_irob_test_h_incl
#define pending_receiver_irob_test_h_incl

#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>
#include "pending_receiver_irob.h"

const size_t BUFSIZE = 50;

class PendingReceiverIROBTest : public CppUnit::TestFixture {
    CPPUNIT_TEST_SUITE(PendingReceiverIROBTest);
    CPPUNIT_TEST(testAllChunksComplete);
    CPPUNIT_TEST(testOutOfOrderChunks);
    CPPUNIT_TEST_SUITE_END();

    static char buffer[BUFSIZE+1];

  public:
    void setUp();
    void tearDown();

    void testAllChunksComplete();
    void testOutOfOrderChunks();

  private:
    void addAChunk(PendingReceiverIROB *prirob,
                   unsigned long seqno, size_t offset, size_t len);
};

#endif
