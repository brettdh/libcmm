LOCAL_PATH := $(call my-dir)

ifneq ($(BDH_FULL_LIBSTDCXX),)

include $(CLEAR_VARS)

LOCAL_PREBUILT_LIBS := libboost_thread.a
include $(BUILD_MULTI_PREBUILT)

include $(CLEAR_VARS)

LOCAL_MODULE := libcmm
LOCAL_CFLAGS += -DANDROID -DCMM_DEBUG -g
LOCAL_SRC_FILES := \
	ack_timeouts.cpp \
	cmm_conn_bootstrapper.cpp \
	cmm_internal_listener.cpp \
	cmm_socket.cpp \
	cmm_socket_control.cpp \
	cmm_socket_impl.cpp \
	cmm_socket_passthrough.cpp \
	cmm_thread.cpp \
	cmm_timing.cpp \
	csocket.cpp \
	csocket_mapping.cpp \
	csocket_receiver.cpp \
	csocket_sender.cpp \
	debug.cpp \
	intset.cpp \
	irob_scheduling.cpp \
	libcmm.cpp \
	libcmm_ipc.cpp \
	libcmm_irob.cpp \
	net_interface.cpp \
	net_stats.cpp \
	pending_irob.cpp \
	pending_receiver_irob.cpp \
	pending_sender_irob.cpp \
	thunks.cpp \
	timeops.cpp

LOCAL_STATIC_LIBRARIES := libboost_thread
LOCAL_PRELINK_MODULE := false
include $(BUILD_SHARED_LIBRARY)

# cmm_test_sender: libcmm_test_sender.o libcmm.so 
#   $(CXX) $(CXXFLAGS) $(LDFLAGS) $(LIBS) -lcmm -o $@ $<

include $(CLEAR_VARS)
LOCAL_MODULE_PATH := $(TARGET_OUT_EXECUTABLES)/libcmm_bin
LOCAL_MODULE := cmm_test_sender
LOCAL_SRC_FILES := libcmm_test_sender.cpp
LOCAL_SHARED_LIBRARIES := libcmm
include $(BUILD_EXECUTABLE)

# cmm_test_receiver: libcmm_test_receiver.o libcmm.so 
#   $(CXX) $(CXXFLAGS) $(LDFLAGS) $(LIBS) -lcmm -o $@ $<

include $(CLEAR_VARS)
LOCAL_MODULE_PATH := $(TARGET_OUT_EXECUTABLES)/libcmm_bin
LOCAL_MODULE := cmm_test_receiver
LOCAL_SRC_FILES := libcmm_test_receiver.cpp
LOCAL_SHARED_LIBRARIES := libcmm
include $(BUILD_EXECUTABLE)

# vanilla_test_sender: vanilla_test_sender.o timeops.o
#   $(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^

include $(CLEAR_VARS)
LOCAL_MODULE_PATH := $(TARGET_OUT_EXECUTABLES)/libcmm_bin
LOCAL_MODULE := vanilla_test_sender
LOCAL_SRC_FILES := libcmm_test_sender.cpp timeops.cpp
LOCAL_CFLAGS += -DNOMULTISOCK
LOCAL_STATIC_LIBRARIES := libboost_thread
include $(BUILD_EXECUTABLE)

# vanilla_test_receiver: vanilla_test_receiver.o timeops.o
#   $(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^

include $(CLEAR_VARS)
LOCAL_MODULE_PATH := $(TARGET_OUT_EXECUTABLES)/libcmm_bin
LOCAL_MODULE := vanilla_test_receiver
LOCAL_SRC_FILES := libcmm_test_receiver.cpp timeops.cpp
LOCAL_CFLAGS += -DNOMULTISOCK
include $(BUILD_EXECUTABLE)

# cmm_throughput_test: libcmm_throughput_test.o libcmm.so
#   $(CXX) $(CXXFLAGS) $(LDFLAGS) $(LIBS) -lcmm -o $@ $<

include $(CLEAR_VARS)
LOCAL_MODULE_PATH := $(TARGET_OUT_EXECUTABLES)/libcmm_bin
LOCAL_MODULE := cmm_throughput_test
LOCAL_SRC_FILES := libcmm_throughput_test.cpp
LOCAL_SHARED_LIBRARIES := libcmm
include $(BUILD_EXECUTABLE)

# vanilla_throughput_test: vanilla_throughput_test.o timeops.o
#   $(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^

include $(CLEAR_VARS)
LOCAL_MODULE_PATH := $(TARGET_OUT_EXECUTABLES)/libcmm_bin
LOCAL_MODULE := vanilla_throughput_test
LOCAL_SRC_FILES := libcmm_throughput_test.cpp timeops.cpp
LOCAL_CFLAGS += -DNOMULTISOCK
include $(BUILD_EXECUTABLE)

# conn_scout: libcmm_scout.o cdf_sampler.o debug.o cmm_thread.o timeops.o

include $(CLEAR_VARS)
LOCAL_MODULE_PATH := $(TARGET_OUT_EXECUTABLES)/libcmm_bin
LOCAL_MODULE := conn_scout
LOCAL_SRC_FILES := \
    libcmm_scout.cpp cdf_sampler.cpp debug.cpp cmm_thread.cpp timeops.cpp
LOCAL_STATIC_LIBRARIES := libboost_thread
include $(BUILD_EXECUTABLE)

endif
