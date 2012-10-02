LOCAL_PATH := $(call my-dir)

NETWORK_TEST_PATH := $(HOME)/src/network_test
NETWORK_TEST_INCLUDES := $(NETWORK_TEST_PATH)/c_source

include $(CLEAR_VARS)

LOCAL_PREBUILT_LIBS := ../../../android_libs/libboost_thread.a
include $(BUILD_MULTI_PREBUILT)

# include $(CLEAR_VARS)

# LOCAL_MODULE=libnative_networktest
# LOCAL_SRC_FILES := libnative_networktest.so
# include $(PREBUILT_SHARED_LIBRARY)

include $(CLEAR_VARS)

LOCAL_MODULE_TAGS := optional
LOCAL_MODULE := conn_scout
LOCAL_SRC_FILES := \
    ../../libcmm_scout.cpp \
    $(addprefix ../../../, debug.cpp cmm_thread.cpp timeops.cpp libcmm_net_restriction.cpp)
LOCAL_CFLAGS += -DBUILDING_SCOUT_SHLIB -DBUILDING_SCOUT -DNDK_BUILD -g -ggdb
LOCAL_C_INCLUDES += $(LOCAL_PATH)/../../../ $(NETWORK_TEST_INCLUDES)
LOCAL_LDLIBS := -L$(LOCAL_PATH)/../../../android_libs -lboost_thread -llog
#LOCAL_SHARED_LIBRARIES := libnative_networktest

include $(BUILD_SHARED_LIBRARY)
