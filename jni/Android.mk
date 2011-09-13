LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE=boost_thread
LOCAL_SRC_FILES := ../android_libs/libboost_thread.a
include $(PREBUILT_STATIC_LIBRARY)

include $(CLEAR_VARS)

LOCAL_MODULE_TAGS := optional
LOCAL_MODULE := libcmm
LOCAL_CFLAGS += -DANDROID -DNDK_BUILD -DCMM_DEBUG -g -ggdb -O0 -I$(LOCAL_PATH)/..
LOCAL_SRC_FILES := $(addprefix ../, \
	cmm_conn_bootstrapper.cpp \
	cmm_internal_listener.cpp \
	cmm_socket.cpp \
	cmm_socket_control.cpp \
	cmm_socket_impl.cpp \
	cmm_socket_passthrough.cpp \
	cmm_thread.cpp \
	cmm_timing.cpp \
	common.cpp \
	csocket.cpp \
	csocket_mapping.cpp \
	csocket_receiver.cpp \
	csocket_sender.cpp \
	debug.cpp \
	intset.cpp \
	irob_scheduling.cpp \
	libcmm.cpp \
	libcmm_external_ipc.cpp \
	libcmm_ipc.cpp \
	libcmm_irob.cpp \
	libcmm_shmem.cpp \
	net_interface.cpp \
	net_stats.cpp \
	pending_irob.cpp \
	pending_receiver_irob.cpp \
	pending_sender_irob.cpp \
	thunks.cpp \
	timeops.cpp)

LOCAL_STATIC_LIBRARIES := libboost_thread
LOCAL_STATIC_LIBRARIES += libancillary
#LOCAL_SHARED_LIBRARIES := liblog
LOCAL_LDLIBS := -llog
LOCAL_PRELINK_MODULE := false
include $(BUILD_SHARED_LIBRARY)

# cmm_test_sender: libcmm_test_sender.o libcmm.so 
#   $(CXX) $(CXXFLAGS) $(LDFLAGS) $(LIBS) -lcmm -o $@ $<

include $(CLEAR_VARS)
LOCAL_MODULE_PATH := $(TARGET_OUT_EXECUTABLES)/libcmm_bin
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE := cmm_test_sender
LOCAL_CFLAGS += -DANDROID -DNDK_BUILD -DCMM_DEBUG -g -ggdb  -O0 -I$(LOCAL_PATH)/..
LOCAL_SRC_FILES := $(addprefix ../, libcmm_test_sender.cpp debug.cpp)
LOCAL_SHARED_LIBRARIES := libcmm liblog
LOCAL_LDLIBS := -llog
include $(BUILD_EXECUTABLE)

# cmm_test_receiver: libcmm_test_receiver.o libcmm.so 
#   $(CXX) $(CXXFLAGS) $(LDFLAGS) $(LIBS) -lcmm -o $@ $<

include $(CLEAR_VARS)
LOCAL_MODULE_PATH := $(TARGET_OUT_EXECUTABLES)/libcmm_bin
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE := cmm_test_receiver
LOCAL_CFLAGS += -DANDROID -DNDK_BUILD -DCMM_DEBUG -g -ggdb  -O0 -I$(LOCAL_PATH)/..
LOCAL_SRC_FILES := $(addprefix ../, libcmm_test_receiver.cpp debug.cpp)
LOCAL_SHARED_LIBRARIES := libcmm liblog
LOCAL_LDLIBS := -llog
include $(BUILD_EXECUTABLE)

# vanilla_test_sender: vanilla_test_sender.o timeops.o
#   $(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^

include $(CLEAR_VARS)
LOCAL_MODULE_PATH := $(TARGET_OUT_EXECUTABLES)/libcmm_bin
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE := vanilla_test_sender
LOCAL_CFLAGS += -DANDROID -DNDK_BUILD -DCMM_DEBUG -g -ggdb  -O0 -I$(LOCAL_PATH)/..
LOCAL_SRC_FILES := $(addprefix ../, libcmm_test_sender.cpp timeops.cpp debug.cpp)
LOCAL_CFLAGS += -DNOMULTISOCK
LOCAL_STATIC_LIBRARIES := libboost_thread
LOCAL_STATIC_LIBRARIES += libancillary
LOCAL_SHARED_LIBRARIES := libcmm liblog
LOCAL_LDLIBS := -llog
include $(BUILD_EXECUTABLE)

# vanilla_test_receiver: vanilla_test_receiver.o timeops.o
#   $(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^

include $(CLEAR_VARS)
LOCAL_MODULE_PATH := $(TARGET_OUT_EXECUTABLES)/libcmm_bin
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE := vanilla_test_receiver
LOCAL_CFLAGS += -DANDROID -DNDK_BUILD -DCMM_DEBUG -g -ggdb  -O0 -I$(LOCAL_PATH)/..
LOCAL_SRC_FILES := $(addprefix ../, libcmm_test_receiver.cpp timeops.cpp debug.cpp)
LOCAL_SHARED_LIBRARIES := libcmm liblog
LOCAL_LDLIBS := -llog
LOCAL_CFLAGS += -DNOMULTISOCK
include $(BUILD_EXECUTABLE)

# cmm_throughput_test: libcmm_throughput_test.o libcmm.so
#   $(CXX) $(CXXFLAGS) $(LDFLAGS) $(LIBS) -lcmm -o $@ $<

include $(CLEAR_VARS)
LOCAL_MODULE_PATH := $(TARGET_OUT_EXECUTABLES)/libcmm_bin
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE := cmm_throughput_test
LOCAL_CFLAGS += -DANDROID -DNDK_BUILD -DCMM_DEBUG -g -ggdb  -O0 -I$(LOCAL_PATH)/..
LOCAL_SRC_FILES := $(addprefix ../, libcmm_throughput_test.cpp debug.cpp)
LOCAL_SHARED_LIBRARIES := libcmm liblog
LOCAL_LDLIBS := -llog
include $(BUILD_EXECUTABLE)

# vanilla_throughput_test: vanilla_throughput_test.o timeops.o
#   $(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^

include $(CLEAR_VARS)
LOCAL_MODULE_PATH := $(TARGET_OUT_EXECUTABLES)/libcmm_bin
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE := vanilla_throughput_test
LOCAL_CFLAGS += -DANDROID -DNDK_BUILD -DCMM_DEBUG -g -ggdb  -O0 -I$(LOCAL_PATH)/..
LOCAL_SRC_FILES := $(addprefix ../, libcmm_throughput_test.cpp timeops.cpp debug.cpp)
LOCAL_CFLAGS += -DNOMULTISOCK
LOCAL_SHARED_LIBRARIES := libcmm liblog
LOCAL_LDLIBS := -llog
include $(BUILD_EXECUTABLE)

# conn_scout: libcmm_scout.o cdf_sampler.o debug.o cmm_thread.o timeops.o

include $(CLEAR_VARS)

LOCAL_MODULE_TAGS := optional
LOCAL_MODULE := conn_scout
LOCAL_SRC_FILES := $(addprefix ../, \
	scout/libcmm_scout.cpp debug.cpp cmm_thread.cpp timeops.cpp cdf_sampler.cpp)
LOCAL_CFLAGS += -DBUILDING_SCOUT -DANDROID -DNDK_BUILD -I$(LOCAL_PATH)/..
LOCAL_C_INCLUDES += $(LOCAL_PATH)/../
LOCAL_STATIC_LIBRARIES := libboost_thread
LOCAL_SHARED_LIBRARIES := liblog
LOCAL_LDLIBS := -llog

include $(BUILD_EXECUTABLE)
