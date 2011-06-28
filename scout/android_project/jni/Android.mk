LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_PREBUILT_LIBS := ../../../android_libs/libboost_thread.a
include $(BUILD_MULTI_PREBUILT)

include $(CLEAR_VARS)

LOCAL_MODULE=networktest_library
LOCAL_SRC_FILES := libnative_networktest.so
include $(PREBUILT_SHARED_LIBRARY)

include $(CLEAR_VARS)

LOCAL_MODULE_TAGS := optional
LOCAL_MODULE := conn_scout
LOCAL_SRC_FILES := \
    ../../libcmm_scout.cpp \
    $(addprefix ../../../, debug.cpp cmm_thread.cpp timeops.cpp)
LOCAL_CFLAGS += -DBUILDING_SCOUT_SHLIB -DBUILDING_SCOUT -DNDK_BUILD
LOCAL_C_INCLUDES += $(LOCAL_PATH)/../../../
LOCAL_LDLIBS := -L$(LOCAL_PATH)/../../../android_libs -lboost_thread -llog

include $(BUILD_SHARED_LIBRARY)
