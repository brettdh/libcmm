#include <cppunit/extensions/HelperMacros.h>
#include "end_to_end_tests_forked.h"
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <libcmm.h>
#include <libcmm_irob.h>
#include "test_common.h"

//CPPUNIT_TEST_SUITE_REGISTRATION(EndToEndTestsForked);

pid_t EndToEndTestsForked::receiver_pid = -1;
EndToEndTestsForked::static_destroyer EndToEndTestsForked::destroyer;

void
EndToEndTestsForked::chooseRole()
{
    receiver_pid = fork();
    handle_error(receiver_pid < 0, "error: didn't fork");
    setRemoteHost("localhost");
}

bool
EndToEndTestsForked::isReceiver()
{
    return receiver_pid == 0;
}

void
EndToEndTestsForked::waitForReceiver()
{
    sleep(2);
    int status = 0;
    int rc = waitpid(receiver_pid, &status, WNOHANG);
    if (rc < 0) {
        fprintf(stderr, "Failed to check receiver status! errno=%d\n", errno);
        exit(EXIT_FAILURE);
    } else if (rc == receiver_pid && WIFEXITED(status)) {
        fprintf(stderr, "Receiver died prematurely, exit code %d\n", status);
        exit(EXIT_FAILURE);
    }
    
    fprintf(stderr, "Receiver started, pid=%d\n", receiver_pid);
}

void
EndToEndTestsForked::testOrderingSimple()
{
    const size_t NUMINTS = 10;
    int nums[NUMINTS] = {
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9
    };

    if (isReceiver()) {
        receiverAssertIntsSorted(nums, NUMINTS);
    } else {
        fprintf(stderr, "Sending 10 sorted integers, in order\n");
        for (size_t i = 0; i < NUMINTS; i++) {
            nums[i] = htonl(nums[i]);
            int rc = cmm_send(data_sock, &nums[i], sizeof(nums[i]), 0,
                              0, NULL, NULL);
            print_on_error(rc < 0, "cmm_send");
            CPPUNIT_ASSERT_EQUAL_MESSAGE("Sending integers", 
                                         (int)sizeof(nums[i]), rc);
        }
    }
}

void
EndToEndTestsForked::testOrderingReverse()
{
    const size_t NUMINTS = 10;
    int nums[NUMINTS] = {
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9
    };

    if (isReceiver()) {
        receiverAssertIntsSorted(nums, NUMINTS);
    } else {
        fprintf(stderr, "Sending 10 sorted integers, ordered by "
               "IROBs, sending in reverse order\n");

        irob_id_t irobs[NUMINTS];
        irobs[0] = begin_irob(data_sock, 0, NULL, 0, NULL, NULL);
        CPPUNIT_ASSERT_MESSAGE("begin_irob succeeds", irobs[0] >= 0);
        for (size_t i = 1; i < NUMINTS; ++i) {
            irobs[i] = begin_irob(data_sock, 1, &irobs[i-1], 0, NULL, NULL);
            CPPUNIT_ASSERT_MESSAGE("begin_irob succeeds", irobs[i] >= 0);
        }
        for (int i = (int)NUMINTS - 1; i >= 0; --i) {
            int rc = irob_send(irobs[i], (char*)&irobs[i], sizeof(irobs[i]), 0);
            CPPUNIT_ASSERT_EQUAL_MESSAGE("irob_send succeeds", 
                                         (int)sizeof(irobs[i]), rc);
            rc = end_irob(irobs[i]);
            CPPUNIT_ASSERT_EQUAL_MESSAGE("end_irob succeeds", 0, rc);
        }
    }
}
