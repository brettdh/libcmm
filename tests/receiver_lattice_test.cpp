#include <cppunit/Test.h>
#include <cppunit/TestAssert.h>
#include <cppunit/extensions/HelperMacros.h>
#include "receiver_lattice_test.h"
#include <sstream>
using std::ostringstream;

CPPUNIT_TEST_SUITE_REGISTRATION(ReceiverLatticeTest);

void
ReceiverLatticeTest::setUp()
{
    pirobs = new PendingReceiverIROBLattice(NULL);

    int *new_int = (int*)(new char[sizeof(int)]);
    *new_int = 0;
    pirob_array[0] = new PendingReceiverIROB(0, 0, NULL, sizeof(int), (char*)new_int, 0);

    new_int = (int*)(new char[sizeof(int)]);
    *new_int = 1;
    pirob_array[1] = new PendingReceiverIROB(1, 0, NULL, sizeof(int), (char*)new_int, 0);

    new_int = (int*)(new char[sizeof(int)]);
    *new_int = 2;
    pirob_array[2] = new PendingReceiverIROB(2, 0, NULL, sizeof(int), (char*)new_int, 0);
    
    new_int = (int*)(new char[sizeof(int)]);
    *new_int = 3;
    pirob_array[3] = new PendingReceiverIROB(3, 0, NULL, sizeof(int), (char*)new_int, 0);
}

void
ReceiverLatticeTest::tearDown()
{
    pirobs->clear();
    delete pirobs;
}

void
ReceiverLatticeTest::assert_insert(irob_id_t id, PendingReceiverIROB *pirob)
{
    CPPUNIT_ASSERT(pirobs->insert(pirob) == true);
    CPPUNIT_ASSERT(get_pointer(pirobs->find(id)) == pirob);
}

void 
ReceiverLatticeTest::testReceive()
{
    for (int i = 0; i < 4; i++) {
        assert_insert(i, pirob_array[i]);
    }
    
    pirobs->release_if_ready(pirob_array[0], ReadyIROB());

    for (int i = 0; i < 4; i++) {
        int num = -1;
        u_long labels = 42;
        ssize_t rc = pirobs->recv((void*)&num, sizeof(int), 0, &labels);
        CPPUNIT_ASSERT(rc == sizeof(int));
        CPPUNIT_ASSERT(num == i);
        CPPUNIT_ASSERT(labels == 0);
    }
}

void
ReceiverLatticeTest::testMultiIROBReceive()
{
    for (int i = 0; i < 4; i++) {
        assert_insert(i, pirob_array[i]);
    }
    
    pirobs->release_if_ready(pirob_array[0], ReadyIROB());

    int buf[4];
    u_long labels = 42;
    ssize_t rc = pirobs->recv((void*)buf, sizeof(int) * 4, MSG_WAITALL, &labels);
    CPPUNIT_ASSERT_EQUAL((int)(sizeof(int) * 4), rc);
    CPPUNIT_ASSERT(labels == 0);
    for (int i = 0; i < 4; i++) {
        CPPUNIT_ASSERT(buf[i] == i);
    }
}
