include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
            $(call all-subdir-java-files)

LOCAL_MODULE_TAGS := optional
LOCAL_MODULE:= intnw

include $(BUILD_JAVA_LIBRARY)
