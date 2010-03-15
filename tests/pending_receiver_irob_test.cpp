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

    prirob = new PendingReceiverIROB(0, 0, NULL, 0, NULL, 0);
    for (size_t i = 0; i < BUFSIZE/10; ++i) {
        char *chunk_data = new char[10];\
        size_t offset = 10*i;
        memcpy(chunk_data, buffer + offset, 10);
        struct irob_chunk_data chunk = {0, i, offset, 10, chunk_data};
        prirob->add_chunk(chunk);
    }
}

void 
PendingReceiverIROBTest::tearDown()
{
    delete prirob;
}

// XXX: disabled this test for now since it conflicts with the new approach
//  to received chunks (that is, there are no partial chunks;
//  the seqnos that are received are the chunks.)
void
PendingReceiverIROBTest::testOverwrite()
{
    const size_t NUMCHUNKSIZES = 6;
    size_t chunksizes[NUMCHUNKSIZES] = {10, 1, 5, 15, 20, BUFSIZE};

    CPPUNIT_ASSERT_EQUAL_MESSAGE("Number of bytes is as expected",
                                 BUFSIZE, (size_t)prirob->numbytes());
    for (size_t i = 0; i < NUMCHUNKSIZES; ++i) {
        size_t chunksize = chunksizes[i];

        for (size_t j = 0; j < BUFSIZE; j += chunksize) {
            struct irob_chunk_data chunk = {0, j/chunksize, 0, chunksize, new char[chunksize]};
            bool ret = prirob->add_chunk(chunk);
            
            CPPUNIT_ASSERT_MESSAGE("add_chunk succeeds", ret);
            CPPUNIT_ASSERT_EQUAL_MESSAGE("Number of bytes is unchanged",
                                         BUFSIZE, (size_t)prirob->numbytes());
            delete [] chunk.data;
        }
    }

    // add overlapping chunk near the end; only the new part should be inserted
    struct irob_chunk_data chunk = {0, -1, BUFSIZE-10, 20, new char[20]};
    memcpy(chunk.data, buffer + BUFSIZE - 10, 10);
    memcpy(chunk.data + 10, buffer + BUFSIZE - 10, 10);

    bool ret = prirob->add_chunk(chunk);
    CPPUNIT_ASSERT_MESSAGE("add_chunk succeeds", ret);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("Number of bytes changed correctly",
                                 BUFSIZE + 10, (size_t)prirob->numbytes());
}

