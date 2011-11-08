LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_MODULE := intnw_javalib
LOCAL_SRC_FILES := ../../obj/local/armeabi/libintnw_javalib.so
include $(PREBUILT_SHARED_LIBRARY)
