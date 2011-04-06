LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_PREBUILT_LIBS := ../../../android_libs/libcppunit.a
include $(BUILD_MULTI_PREBUILT)

include $(CLEAR_VARS)

LOCAL_PREBUILT_LIBS := ../../../android_libs/libboost_thread.a
include $(BUILD_MULTI_PREBUILT)

include $(CLEAR_VARS)

common_C_INCLUDES := \
	$(addprefix /Users/brettdh/src/android-source/,\
		external/bdh_apps/cppunit/include \
                external/bdh_apps/libcmm \
                external/openssl/include)
common_CFLAGS:=-DANDROID -DNDK_BUILD -DCMM_UNIT_TESTING -DCMM_DEBUG -g -O0

LOCAL_MODULE := run_remote_tests
LOCAL_SRC_FILES := \
    ./android_run_tests.cpp \
    $(addprefix ../../, end_to_end_tests_base.cpp \
			end_to_end_tests_remote.cpp \
			remote_tests.cpp \
			socket_api_tests.cpp \
			test_common.cpp)

LOCAL_C_INCLUDES := $(common_C_INCLUDES)
LOCAL_CFLAGS := $(common_CFLAGS)
LOCAL_LDLIBS := -L$(LOCAL_PATH)/../../../android_libs \
		-L/Users/brettdh/src/android-source/out/target/product/generic/obj/lib \
		-Wl,-rpath-link=/Users/brettdh/src/android-source/out/target/product/generic/obj/lib \
	        -lcmm -lssl -lcrypto -lcppunit -lboost_thread -llog

include $(BUILD_SHARED_LIBRARY)
