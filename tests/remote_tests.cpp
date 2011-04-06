#include <cppunit/extensions/HelperMacros.h>
#include "remote_tests.h"
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <openssl/sha.h>
#include <assert.h>
#include <libcmm.h>
#include <libcmm_irob.h>

CPPUNIT_TEST_SUITE_REGISTRATION(RemoteTests);

void 
RemoteTests::testDefaultIROBOrdering()
{
    const size_t NUMINTS = 10;
    int nums[NUMINTS] = {
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9
    };

    if (isReceiver()) {
        receiverAssertIntsSorted(nums, NUMINTS);
    } else {
        for (size_t i = 0; i < NUMINTS; ++i) {
            u_long label = ((i % 2 == 0) 
                            ? CMM_LABEL_ONDEMAND 
                            : CMM_LABEL_BACKGROUND);
            nums[i] = htonl(nums[i]);
            int rc = cmm_send(data_sock, &nums[i], sizeof(nums[i]), 0,
                              label, NULL, NULL);
            CPPUNIT_ASSERT_EQUAL_MESSAGE("Sending integers", 
                                         (int)sizeof(nums[i]), rc);

        }
    }
}
