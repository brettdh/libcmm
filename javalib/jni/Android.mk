LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

MY_ANDROID_SRC_ROOT := $(HOME)/src/android-source
LIBCMM_ROOT := $(MY_ANDROID_SRC_ROOT)/external/bdh_apps/libcmm

LOCAL_MODULE := cmm
LOCAL_SRC_FILES := ../../obj/local/armeabi/libcmm.so
include $(PREBUILT_SHARED_LIBRARY)

include $(CLEAR_VARS)

LOCAL_SRC_FILES := wrappers.cpp ../../debug.cpp
LOCAL_CFLAGS += -DANDROID -DNDK_BUILD -DCMM_DEBUG -g -ggdb -O0
LOCAL_LDLIBS := -llog

LOCAL_C_INCLUDES += \
        $(LIBCMM_ROOT)

LOCAL_SHARED_LIBRARIES := \
        libcmm

LOCAL_MODULE_TAGS := optional
LOCAL_MODULE:= libintnw_javalib

include $(BUILD_SHARED_LIBRARY)
