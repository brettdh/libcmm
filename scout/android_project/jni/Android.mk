LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_PREBUILT_LIBS := ../../../libboost_thread.a
include $(BUILD_MULTI_PREBUILT)

include $(CLEAR_VARS)

LOCAL_MODULE := conn_scout
LOCAL_SRC_FILES := \
    ../../libcmm_scout.cpp \
    $(addprefix ../../../, debug.cpp cmm_thread.cpp timeops.cpp)
LOCAL_CFLAGS += -DBUILDING_SCOUT_SHLIB
LOCAL_C_INCLUDES += $(LOCAL_PATH)/../../../
LOCAL_LDLIBS := -L$(LOCAL_PATH)/../../.. -lboost_thread

include $(BUILD_SHARED_LIBRARY)
