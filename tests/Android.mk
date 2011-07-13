LOCAL_PATH := $(call my-dir)

ifneq ($(BDH_FULL_LIBSTDCXX),)

common_C_INCLUDES := external/bdh_apps/cppunit/include \
                     external/bdh_apps/libcmm \
                     external/openssl/include
common_CFLAGS:=-DANDROID -DCMM_UNIT_TESTING -DCMM_DEBUG -g -O0
common_STATIC_LIBRARIES:=libcppunit libboost_thread
TESTSUITE_SRCS := run_all_tests.cpp test_common.cpp StdioOutputter.cpp

# unit tests
include $(CLEAR_VARS)
TEST_SRCS := pending_irob_test.cpp lattice_test.cpp intset_test.cpp \
	    receiver_lattice_test.cpp pending_sender_irob_test.cpp \
	    ack_timeouts_test.cpp pending_receiver_irob_test.cpp \
	    estimation_test.cpp pthread_util_test.cpp
SUPPORT_SRCS := pending_irob.cpp intset.cpp debug.cpp pending_receiver_irob.cpp \
	       pending_sender_irob.cpp timeops.cpp ack_timeouts.cpp net_stats.cpp \
	       net_interface.cpp irob_scheduling.cpp

LOCAL_MODULE_PATH := $(TARGET_OUT_EXECUTABLES)/libcmm_tests
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE := run_unit_tests
LOCAL_SRC_FILES := $(TESTSUITE_SRCS) \
                   $(TEST_SRCS) $(addprefix ../, $(SUPPORT_SRCS))
LOCAL_C_INCLUDES := $(common_C_INCLUDES)
LOCAL_CFLAGS := $(common_CFLAGS)
LOCAL_SHARED_LIBRARIES := liblog
LOCAL_STATIC_LIBRARIES := $(common_STATIC_LIBRARIES)
include $(BUILD_EXECUTABLE)

SHLIBS := libcmm libssl

# forked tests
include $(CLEAR_VARS)
LIBTEST_SRCS := end_to_end_tests_base.cpp end_to_end_tests_forked.cpp \
                forked_tests.cpp
LOCAL_MODULE_PATH := $(TARGET_OUT_EXECUTABLES)/libcmm_tests
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE := run_lib_tests
LOCAL_SRC_FILES := $(TESTSUITE_SRCS) \
                   $(LIBTEST_SRCS)
LOCAL_C_INCLUDES := $(common_C_INCLUDES)
LOCAL_CFLAGS := $(common_CFLAGS)
LOCAL_STATIC_LIBRARIES := $(common_STATIC_LIBRARIES)
LOCAL_SHARED_LIBRARIES := $(SHLIBS)
include $(BUILD_EXECUTABLE)

# remote tests
include $(CLEAR_VARS)
REMOTETEST_SRCS := end_to_end_tests_base.cpp end_to_end_tests_remote.cpp \
                   remote_tests.cpp
LOCAL_MODULE_PATH := $(TARGET_OUT_EXECUTABLES)/libcmm_tests
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE := run_remote_tests
LOCAL_SRC_FILES := $(TESTSUITE_SRCS) \
                   $(REMOTETEST_SRCS)
LOCAL_C_INCLUDES := $(common_C_INCLUDES)
LOCAL_CFLAGS := $(common_CFLAGS)
LOCAL_STATIC_LIBRARIES := $(common_STATIC_LIBRARIES)
LOCAL_SHARED_LIBRARIES := $(SHLIBS)
include $(BUILD_EXECUTABLE)


# non-blocking tests
include $(CLEAR_VARS)
NB_SRCS := end_to_end_tests_base.cpp non_blocking_tests.cpp
LIBTESTNB_SRCS := end_to_end_tests_forked.cpp non_blocking_tests_forked.cpp
REMOTETESTNB_SRCS := end_to_end_tests_remote.cpp \
                     non_blocking_tests_remote.cpp

LOCAL_MODULE_PATH := $(TARGET_OUT_EXECUTABLES)/libcmm_tests
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE := run_lib_tests_nb
LOCAL_SRC_FILES := $(TESTSUITE_SRCS) \
                   $(NB_SRCS) $(LIBTESTNB_SRCS)
LOCAL_C_INCLUDES := $(common_C_INCLUDES)
LOCAL_CFLAGS := $(common_CFLAGS)
LOCAL_STATIC_LIBRARIES := $(common_STATIC_LIBRARIES)
LOCAL_SHARED_LIBRARIES := $(SHLIBS)
include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)
LOCAL_MODULE_PATH := $(TARGET_OUT_EXECUTABLES)/libcmm_tests
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE := run_remote_tests_nb
LOCAL_SRC_FILES := $(TESTSUITE_SRCS) \
                   $(NB_SRCS) $(REMOTETESTNB_SRCS)
LOCAL_C_INCLUDES := $(common_C_INCLUDES)
LOCAL_CFLAGS := $(common_CFLAGS)
LOCAL_STATIC_LIBRARIES := $(common_STATIC_LIBRARIES)
LOCAL_SHARED_LIBRARIES := $(SHLIBS)
include $(BUILD_EXECUTABLE)

# thunk tests
include $(CLEAR_VARS)
THUNKTEST_SRCS:=end_to_end_tests_base.cpp end_to_end_tests_remote.cpp \
                thunk_tests.cpp
LOCAL_MODULE_PATH := $(TARGET_OUT_EXECUTABLES)/libcmm_tests
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE := run_thunk_tests
LOCAL_SRC_FILES := $(TESTSUITE_SRCS) $(THUNKTEST_SRCS)
LOCAL_C_INCLUDES := $(common_C_INCLUDES)
LOCAL_CFLAGS := $(common_CFLAGS)
LOCAL_STATIC_LIBRARIES := $(common_STATIC_LIBRARIES)
LOCAL_SHARED_LIBRARIES := $(SHLIBS)
include $(BUILD_EXECUTABLE)

# trickling tests
include $(CLEAR_VARS)
TRICKLETEST_SRCS:=end_to_end_tests_base.cpp end_to_end_tests_remote.cpp \
                  trickle_tests.cpp
LOCAL_MODULE_PATH := $(TARGET_OUT_EXECUTABLES)/libcmm_tests
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE := run_trickle_tests
LOCAL_SRC_FILES := $(TESTSUITE_SRCS) $(TRICKLETEST_SRCS)
LOCAL_C_INCLUDES := $(common_C_INCLUDES)
LOCAL_CFLAGS := $(common_CFLAGS)
LOCAL_STATIC_LIBRARIES := $(common_STATIC_LIBRARIES)
LOCAL_SHARED_LIBRARIES := $(SHLIBS)
include $(BUILD_EXECUTABLE)



endif # BDH_FULL_LIBSTDCXX
