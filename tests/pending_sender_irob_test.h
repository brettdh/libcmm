#ifndef pending_sender_irob_test_h_incl
#define pending_sender_irob_test_h_incl

#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>
#include "pending_sender_irob.h"

const size_t BUFSIZE = 50;

class PendingSenderIROBTest : public CppUnit::TestFixture {
    PendingSenderIROB *psirob;

    CPPUNIT_TEST_SUITE(PendingSenderIROBTest);
    CPPUNIT_TEST(testMemcpyIovecs);
    CPPUNIT_TEST(testFindChunkByOffset);
    CPPUNIT_TEST(testReadChunks);
    CPPUNIT_TEST(testBreakItUp);
    CPPUNIT_TEST(testOneByteAtATime);
    CPPUNIT_TEST(testGroupChunks);
    CPPUNIT_TEST_SUITE_END();

    static char buffer[BUFSIZE+1];

  public:
    void setUp();
    void tearDown();

    void testMemcpyIovecs();
    void testFindChunkByOffset();

    void testReadChunks();
    void testBreakItUp();
    void testOneByteAtATime();
    void testGroupChunks();

  private:
    void testReadByChunkSize(ssize_t chunksize);
};

#endif
