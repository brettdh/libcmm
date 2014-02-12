LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

LOCAL_SRC_FILES := wrappers.cpp # ../../debug.cpp
LOCAL_CFLAGS += -DANDROID -DNDK_BUILD -DCMM_DEBUG -g -ggdb -O0
LOCAL_C_INCLUDES += $(LOCAL_PATH)/../..
LOCAL_LDLIBS := -llog

LOCAL_SHARED_LIBRARIES := \
        libcmm instruments

LOCAL_MODULE_TAGS := optional
LOCAL_MODULE:= libintnw_javalib

include $(BUILD_SHARED_LIBRARY)

$(call import-module, edu.umich.mobility/configumerator)
$(call import-module, edu.umich.mobility/instruments)
$(call import-module, edu.umich.mobility/libpowertutor)
$(call import-module, edu.umich.mobility/mocktime)
$(call import-module, edu.umich.mobility/libcmm)
