LOCAL_PATH := $(call my-dir)

ifneq ($(BDH_FULL_LIBSTDCXX),)

include $(CLEAR_VARS)

LOCAL_MODULE_TAGS := optional
LOCAL_MODULE := libancillary
LOCAL_CFLAGS += -g -O2
LOCAL_SRC_FILES := fd_send.c fd_recv.c
include $(BUILD_STATIC_LIBRARY)

endif # BDH_FULL_LIBSTDCXX
