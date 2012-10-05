LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE:=cmm
LOCAL_SRC_FILES:=../../obj/local/armeabi/libcmm.so
include $(PREBUILT_SHARED_LIBRARY)

include $(CLEAR_VARS)

common_C_INCLUDES := \
	$(addprefix /Users/brettdh/src/android-source/,\
                external/bdh_apps/libcmm)
common_CFLAGS:=-g -O0 -Wall -Werror

LOCAL_MODULE_TAGS := optional
LOCAL_MODULE := intnw_ops
LOCAL_SRC_FILES := ./intnw_ops.cpp

LOCAL_C_INCLUDES := $(common_C_INCLUDES)
LOCAL_CFLAGS := $(common_CFLAGS)
LOCAL_LDLIBS := -llog
LOCAL_SHARED_LIBRARIES := cmm

include $(BUILD_SHARED_LIBRARY)
