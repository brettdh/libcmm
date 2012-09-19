LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := cmm
LOCAL_SRC_FILES := ../../../libs/armeabi/libcmm.so
include $(PREBUILT_SHARED_LIBRARY)

include $(CLEAR_VARS)

LOCAL_MODULE := cppunit
LOCAL_SRC_FILES := ../../../android_libs/libcppunit.a
include $(PREBUILT_STATIC_LIBRARY)

include $(CLEAR_VARS)

LOCAL_MODULE := boost_thread
LOCAL_SRC_FILES := ../../../android_libs/libboost_thread.a
include $(PREBUILT_STATIC_LIBRARY)

include $(CLEAR_VARS)

LOCAL_MODULE := ssl
LOCAL_SRC_FILES := prebuilt/libssl.so
include $(PREBUILT_SHARED_LIBRARY)

include $(CLEAR_VARS)

LOCAL_MODULE := crypto
LOCAL_SRC_FILES := prebuilt/libcrypto.so
include $(PREBUILT_SHARED_LIBRARY)

include $(CLEAR_VARS)

common_C_INCLUDES := \
	$(addprefix /Users/brettdh/src/android-source/,\
		external/bdh_apps/cppunit/include \
                external/bdh_apps/libcmm \
                external/openssl/include)
common_CFLAGS:=-DANDROID -DNDK_BUILD -DCMM_UNIT_TESTING -DCMM_DEBUG -g -O0

LOCAL_MODULE_TAGS := optional
LOCAL_MODULE := run_remote_tests
LOCAL_SRC_FILES := \
     ./android_run_tests.cpp \
     $(addprefix ../../, end_to_end_tests_base.cpp \
			end_to_end_tests_remote.cpp \
			test_common.cpp) \
	$(addprefix ../../../, net_interface.cpp cmm_socket_control.cpp debug.cpp)
#	$(addprefix ../../, spotty_network_failure_test.cpp)


LOCAL_C_INCLUDES := $(common_C_INCLUDES)
LOCAL_CFLAGS := $(common_CFLAGS)
LOCAL_LDLIBS := -llog
LOCAL_STATIC_LIBRARIES := libcppunit
LOCAL_SHARED_LIBRARIES := libcmm libssl libcrypto

include $(BUILD_SHARED_LIBRARY)


include $(CLEAR_VARS)

LOCAL_MODULE := proxy_socket_test
LOCAL_C_INCLUDES := $(common_C_INCLUDES)
LOCAL_CFLAGS := $(common_CFLAGS)
LOCAL_SRC_FILES := \
	$(addprefix ../../, proxy_socket_test.cpp proxy_socket.cpp test_common.cpp) \
	$(addprefix ../../../, debug.cpp timeops.cpp)
include $(BUILD_EXECUTABLE)

# spotty network failure test

include $(CLEAR_VARS)
TESTSUITE_SRCS := $(addprefix ../../, run_all_tests.cpp test_common.cpp StdioOutputter.cpp)
SPOTTYTEST_SRCS:= \
	$(addprefix ../../, spotty_network_failure_test.cpp end_to_end_tests_base.cpp \
				        end_to_end_tests_remote.cpp proxy_socket.cpp) \
    $(addprefix ../../../, net_interface.cpp cmm_socket_control.cpp)
LOCAL_MODULE := fake_intnw_test
LOCAL_SRC_FILES := $(TESTSUITE_SRCS) $(SPOTTYTEST_SRCS)
LOCAL_C_INCLUDES := $(common_C_INCLUDES)
LOCAL_CFLAGS := $(common_CFLAGS)
LOCAL_LDLIBS := -llog
LOCAL_STATIC_LIBRARIES := libcppunit
LOCAL_SHARED_LIBRARIES := libcmm
include $(BUILD_EXECUTABLE)

# spotty network failure test runner

include $(CLEAR_VARS)
RUNNER_SRCS:=../../run_spotty_network_failure_test.cpp
LOCAL_MODULE := run_spotty_network_failure_test
LOCAL_SRC_FILES := $(RUNNER_SRCS)
LOCAL_C_INCLUDES := $(common_C_INCLUDES)
LOCAL_CFLAGS := $(common_CFLAGS)
include $(BUILD_EXECUTABLE)
