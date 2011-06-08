#include <cppunit/Test.h>
#include <cppunit/TestAssert.h>
#include <cppunit/extensions/HelperMacros.h>
#include "pending_receiver_irob_test.h"
#include <vector>
#include <functional>
using std::vector; using std::min;

CPPUNIT_TEST_SUITE_REGISTRATION(PendingReceiverIROBTest);

char PendingReceiverIROBTest::buffer[BUFSIZE+1];

void 
PendingReceiverIROBTest::setUp()
{
    for (size_t i = 0; i < BUFSIZE; ++i) {
        buffer[i] = 'A' + i;
    }
    buffer[BUFSIZE] = '\0';
}

void 
PendingReceiverIROBTest::tearDown()
{
}

void
PendingReceiverIROBTest::addAChunk(PendingReceiverIROB *prirob,
                                   u_long seqno, size_t offset, size_t len)
{
    assert(offset + len < BUFSIZE);
    char *chunk_data = new char[len+1];
    memcpy(chunk_data, buffer + offset, len);
    chunk_data[len] = '\0';
    
    struct irob_chunk_data chunk;
    chunk.id = 0;
    chunk.seqno = seqno;
    chunk.offset = offset;
    chunk.datalen = len;
    chunk.data = chunk_data;
    prirob->add_chunk(chunk);
}

void
PendingReceiverIROBTest::testAllChunksComplete()
{
    PendingReceiverIROB *prirob = 
        new PendingReceiverIROB(0, 0, NULL, 0, NULL, 0);

    CPPUNIT_ASSERT_MESSAGE("Empty IROB has all chunks complete",
                           prirob->all_chunks_complete());

    addAChunk(prirob, 1, 10, 10);

    CPPUNIT_ASSERT_MESSAGE("IROB with missing chunk doesn't have all chunks complete",
                           !prirob->all_chunks_complete());

    addAChunk(prirob, 0, 0, 10);

    CPPUNIT_ASSERT_MESSAGE("IROB with missing chunk filled in has all chunks complete",
                           prirob->all_chunks_complete());
    
    delete prirob;
}

void
PendingReceiverIROBTest::testOutOfOrderChunks()
{
    PendingReceiverIROBLattice lattice(NULL);

    // create placeholder
    PendingReceiverIROB *placeholder = 
        (PendingReceiverIROB *) lattice.make_placeholder(0);
    CPPUNIT_ASSERT(!placeholder->is_complete());
    CPPUNIT_ASSERT_EQUAL(0, placeholder->numbytes());
    CPPUNIT_ASSERT_EQUAL(0, placeholder->recvdbytes());

    // add chunks
    addAChunk(placeholder, 1, 10, 10);
    addAChunk(placeholder, 2, 20, 10);
    CPPUNIT_ASSERT(!placeholder->is_complete());
    CPPUNIT_ASSERT_EQUAL(20, placeholder->numbytes());
    CPPUNIT_ASSERT_EQUAL(20, placeholder->recvdbytes());

    // create real IROB and subsume placeholder
    PendingReceiverIROB *irob = new PendingReceiverIROB(0, 0, NULL, 0, NULL, 0);
    irob->subsume(placeholder);
    delete placeholder;
    
    CPPUNIT_ASSERT(!irob->is_complete());
    CPPUNIT_ASSERT_EQUAL(20, irob->numbytes());
    CPPUNIT_ASSERT_EQUAL(20, irob->recvdbytes());

    // add chunks
    addAChunk(irob, 0, 0, 10);
    CPPUNIT_ASSERT(!irob->is_complete());
    CPPUNIT_ASSERT_EQUAL(30, irob->numbytes());
    CPPUNIT_ASSERT_EQUAL(30, irob->recvdbytes());

    addAChunk(irob, 3, 30, 10);
    CPPUNIT_ASSERT(!irob->is_complete());
    CPPUNIT_ASSERT_EQUAL(40, irob->numbytes());
    CPPUNIT_ASSERT_EQUAL(40, irob->recvdbytes());

    irob->finish(40, 4);
    CPPUNIT_ASSERT(irob->is_complete());
    CPPUNIT_ASSERT(irob->is_ready());

    // read data
    char buf[40];
    ssize_t rc = irob->read_data(buf, 15);
    CPPUNIT_ASSERT_EQUAL(15, rc);
    CPPUNIT_ASSERT_EQUAL(0, memcmp(buf, buffer, 15));
    CPPUNIT_ASSERT_EQUAL(25, irob->numbytes());
    CPPUNIT_ASSERT_EQUAL(40, irob->recvdbytes());

    rc = irob->read_data(buf + 15, 25);
    CPPUNIT_ASSERT_EQUAL(25, rc);
    CPPUNIT_ASSERT_EQUAL(0, memcmp(buf, buffer, 40));
    CPPUNIT_ASSERT_EQUAL(0, irob->numbytes());
    CPPUNIT_ASSERT_EQUAL(40, irob->recvdbytes());

    delete irob;
}
