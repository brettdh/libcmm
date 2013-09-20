LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

MY_ANDROID_SRC_ROOT := $(HOME)/src/android-source
LIBCMM_ROOT := $(MY_ANDROID_SRC_ROOT)/external/bdh_apps/libcmm

INSTRUMENTS_ROOT := ../../../../../../../../$(HOME)/src/instruments
LIBPT_ROOT := ../../libpowertutor/cpp_source
MOCKTIME_ROOT := ../../mocktime

include $(CLEAR_VARS)

LOCAL_MODULE=instruments
LOCAL_SRC_FILES := ../$(INSTRUMENTS_ROOT)/obj/local/$(TARGET_ARCH_ABI)/libinstruments.so
include $(PREBUILT_SHARED_LIBRARY)

include $(CLEAR_VARS)

LOCAL_MODULE=powertutor
LOCAL_SRC_FILES := ../$(LIBPT_ROOT)/obj/local/$(TARGET_ARCH_ABI)/libpowertutor.so
include $(PREBUILT_SHARED_LIBRARY)

include $(CLEAR_VARS)

LOCAL_MODULE=mocktime
LOCAL_SRC_FILES := ../$(MOCKTIME_ROOT)/obj/local/$(TARGET_ARCH_ABI)/libmocktime.so
include $(PREBUILT_SHARED_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := cmm
LOCAL_SRC_FILES := ../../obj/local/$(TARGET_ARCH_ABI)/libcmm.so
include $(PREBUILT_SHARED_LIBRARY)

include $(CLEAR_VARS)

LOCAL_SRC_FILES := wrappers.cpp # ../../debug.cpp
LOCAL_CFLAGS += -DANDROID -DNDK_BUILD -DCMM_DEBUG -g -ggdb -O0
LOCAL_LDLIBS := -llog

LOCAL_C_INCLUDES += \
        $(LIBCMM_ROOT) $(INSTRUMENTS_ROOT)/include $(INSTRUMENTS_ROOT)/src

LOCAL_SHARED_LIBRARIES := \
        libcmm

LOCAL_MODULE_TAGS := optional
LOCAL_MODULE:= libintnw_javalib

include $(BUILD_SHARED_LIBRARY)
