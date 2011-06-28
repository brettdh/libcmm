LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES:= \
    edu_umich_intnw_lib_MultiSocket.cpp \
    edu_umich_intnw_lib_ServerMultiSocket.cpp

LOCAL_C_INCLUDES += \
        $(JNI_H_INCLUDE)

LOCAL_SHARED_LIBRARIES := \
        libcmm

LOCAL_MODULE_TAGS := optional
LOCAL_MODULE:= libintnw_javalib

include $(BUILD_SHARED_LIBRARY)
