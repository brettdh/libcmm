LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

common_C_INCLUDES := \
	$(addprefix /Users/brettdh/src/android-source/,\
                external/bdh_apps/libcmm)
common_CFLAGS:=-g -O0 -Wall -Werror

LOCAL_MODULE := intnw_ops
LOCAL_SRC_FILES := ./intnw_ops.cpp

LOCAL_C_INCLUDES := $(common_C_INCLUDES)
LOCAL_CFLAGS := $(common_CFLAGS)
LOCAL_LDLIBS := -L/Users/brettdh/src/android-source/out/target/product/generic/obj/lib \
		-Wl,-rpath-link=/Users/brettdh/src/android-source/out/target/product/generic/obj/lib \
		-lcmm -llog

include $(BUILD_SHARED_LIBRARY)
