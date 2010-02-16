#include <cppunit/Test.h>
#include <cppunit/TestAssert.h>
#include <cppunit/extensions/HelperMacros.h>
#include "pending_sender_irob_test.h"
#include <vector>
#include <functional>
#include <deque>
#include <sstream>
using std::vector; using std::min;
using std::deque; using std::ostringstream;

CPPUNIT_TEST_SUITE_REGISTRATION(PendingSenderIROBTest);

char PendingSenderIROBTest::buffer[BUFSIZE+1];

void 
PendingSenderIROBTest::setUp()
{
    for (size_t i = 0; i < BUFSIZE; ++i) {
        buffer[i] = 'A' + i;
    }
    buffer[BUFSIZE] = '\0';

    psirob = new PendingSenderIROB(0, 0, NULL, 0, NULL, 0, NULL, NULL);
    for (size_t i = 0; i < BUFSIZE/10; ++i) {
        char *chunk_data = new char[10];
        size_t offset = 10*i;
        memcpy(chunk_data, buffer + offset, 10);
        struct irob_chunk_data chunk = {0, 0, offset, 10, chunk_data};
        psirob->add_chunk(chunk);
    }
}

size_t memcpy_iovecs(char *dst, const vector<struct iovec>& vecs, size_t n)
{
    size_t bytes_copied = 0;
    size_t i = 0;
    while (bytes_copied < n && i < vecs.size()) {
        size_t bytes_left = n - bytes_copied;
        size_t bytes = min(bytes_left, vecs[i].iov_len);
        memcpy(dst + bytes_copied, vecs[i].iov_base, bytes);
        bytes_copied += bytes;
        i++;
    }

    return bytes_copied;
}

void 
PendingSenderIROBTest::tearDown()
{
    delete psirob;
}

void
PendingSenderIROBTest::testMemcpyIovecs()
{
    vector<struct iovec> vecs;
    for (size_t i = 0; i < BUFSIZE/10; ++i) {
        struct iovec vec = {buffer + (10*i), 10};
        vecs.push_back(vec);
    }

    char bufcopy[BUFSIZE];
    size_t bytes_copied = memcpy_iovecs(bufcopy, vecs, BUFSIZE);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("memcpy_iovecs copied the bytes",
                                 BUFSIZE, bytes_copied);
    int rc = memcmp(buffer, bufcopy, BUFSIZE);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("Buffer copied from iovecs matches original",
                                 0, rc);
}

void
PendingSenderIROBTest::testFindChunkByOffset()
{
    deque<struct irob_chunk_data>::iterator it;
    for (size_t i = 0; i < BUFSIZE; ++i) {
        u_long expected_seqno = i / 10;
        it = psirob->find_app_chunk(i);
        ostringstream oss;
        oss << "Got correct chunk for offset " << i;
        CPPUNIT_ASSERT_EQUAL_MESSAGE(oss.str(),
                                     expected_seqno, it->seqno);
    }
}

void
PendingSenderIROBTest::testReadByChunkSize(ssize_t chunksize)
{
    size_t bytes_copied = 0;
    vector<struct iovec> vecs;
    u_long seqno = 0;
    u_long expected_seqno = 0;

    while (bytes_copied < BUFSIZE) {
        ssize_t bytes = chunksize;
        size_t offset = 0;

        vector<struct iovec> new_vecs = psirob->get_ready_bytes(bytes, seqno, 
                                                                offset);
        CPPUNIT_ASSERT_MESSAGE("Copied a chunk", 
                               (bytes == ((chunksize == 0) ? 10 : chunksize) ||
                                bytes == (ssize_t)(BUFSIZE - bytes_copied)));
        CPPUNIT_ASSERT_EQUAL_MESSAGE("Incremented seqno", expected_seqno, seqno);
        CPPUNIT_ASSERT_EQUAL_MESSAGE("Correct offset", offset, bytes_copied);
        //psirob->mark_sent(bytes);
        vecs.insert(vecs.end(), new_vecs.begin(), new_vecs.end());
        bytes_copied += bytes;
        expected_seqno++;
    }
    CPPUNIT_ASSERT_EQUAL_MESSAGE("Copied all the bytes", BUFSIZE, bytes_copied);

    char bufcopy[BUFSIZE];
    size_t bytes = memcpy_iovecs(bufcopy, vecs, BUFSIZE);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("Copied the iovecs", BUFSIZE, bytes);
    int rc = memcmp(buffer, bufcopy, BUFSIZE);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("Buffers match", 0, rc);
}

void 
PendingSenderIROBTest::testReadChunks()
{
    testReadByChunkSize(0);
}

void 
PendingSenderIROBTest::testBreakItUp()
{
    testReadByChunkSize(4);
}

void 
PendingSenderIROBTest::testOneByteAtATime()
{
    testReadByChunkSize(1);
}

void
PendingSenderIROBTest::testGroupChunks()
{
    testReadByChunkSize(20);
}
