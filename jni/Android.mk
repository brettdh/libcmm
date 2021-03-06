LOCAL_PATH := $(call my-dir)

OPTI := -O3

common_CFLAGS := -DANDROID -DNDK_BUILD -g -ggdb $(OPTI) -std=c++11 -I$(LOCAL_PATH)/..

include $(CLEAR_VARS)

LOCAL_MODULE_TAGS := optional
LOCAL_MODULE := libcmm
LOCAL_CFLAGS += $(common_CFLAGS) -DCMM_DEBUG
LOCAL_EXPORT_C_INCLUDES := $(LOCAL_PATH)/..

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
	flipflop_estimate.cpp \
	intnw_config.cpp \
	intnw_instruments_network_chooser.cpp \
	intnw_instruments_net_stats_wrapper.cpp \
	intset.cpp \
	irob_scheduling.cpp \
	libcmm.cpp \
	libcmm_external_ipc.cpp \
	libcmm_ipc.cpp \
	libcmm_irob.cpp \
	libcmm_net_restriction.cpp \
	libcmm_shmem.cpp \
	net_interface.cpp \
	net_stats.cpp \
	network_chooser.cpp \
	network_chooser_impl.cpp \
	pending_irob.cpp \
	pending_receiver_irob.cpp \
	pending_sender_irob.cpp \
	redundancy_strategy.cpp \
	thunks.cpp \
	timeops.cpp)


LOCAL_STATIC_LIBRARIES += libancillary configumerator
LOCAL_SHARED_LIBRARIES := libinstruments libpowertutor libmocktime
LOCAL_LDLIBS := -llog
LOCAL_PRELINK_MODULE := false
include $(BUILD_SHARED_LIBRARY)


include $(CLEAR_VARS)
LOCAL_MODULE := flipflop
LOCAL_SRC_FILES := $(addprefix ../, flipflop_estimate.cpp debug_ext.cpp)
LOCAL_CFLAGS += $(common_CFLAGS)
include $(BUILD_STATIC_LIBRARY)

# This target is necessary in order to build the static library at all.
include $(CLEAR_VARS)
LOCAL_MODULE := flipflop_shared
LOCAL_STATIC_LIBRARIES := flipflop

include $(BUILD_SHARED_LIBRARY)

# cmm_test_sender: libcmm_test_sender.o libcmm.so 
#   $(CXX) $(CXXFLAGS) $(LDFLAGS) $(LIBS) -lcmm -o $@ $<

include $(CLEAR_VARS)
LOCAL_MODULE_PATH := $(TARGET_OUT_EXECUTABLES)/libcmm_bin
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE := cmm_test_sender
LOCAL_CFLAGS += $(common_CFLAGS)
LOCAL_CFLAGS += -DANDROID -DNDK_BUILD -DCMM_DEBUG -g -ggdb  $(OPTI) -I$(LOCAL_PATH)/..
LOCAL_SRC_FILES := $(addprefix ../, libcmm_test_sender.cpp)
LOCAL_SHARED_LIBRARIES := libcmm liblog libinstruments libpowertutor libmocktime
LOCAL_LDLIBS := -llog
include $(BUILD_EXECUTABLE)

# cmm_test_receiver: libcmm_test_receiver.o libcmm.so 
#   $(CXX) $(CXXFLAGS) $(LDFLAGS) $(LIBS) -lcmm -o $@ $<

include $(CLEAR_VARS)
LOCAL_MODULE_PATH := $(TARGET_OUT_EXECUTABLES)/libcmm_bin
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE := cmm_test_receiver
LOCAL_CFLAGS += $(common_CFLAGS)
LOCAL_CFLAGS += -DANDROID -DNDK_BUILD -DCMM_DEBUG -g -ggdb  $(OPTI) -I$(LOCAL_PATH)/..
LOCAL_SRC_FILES := $(addprefix ../, libcmm_test_receiver.cpp)
LOCAL_SHARED_LIBRARIES := libcmm liblog libinstruments libpowertutor libmocktime
LOCAL_LDLIBS := -llog
include $(BUILD_EXECUTABLE)

# vanilla_test_sender: vanilla_test_sender.o timeops.o
#   $(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^

include $(CLEAR_VARS)
LOCAL_MODULE_PATH := $(TARGET_OUT_EXECUTABLES)/libcmm_bin
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE := vanilla_test_sender
LOCAL_CFLAGS += $(common_CFLAGS)
LOCAL_CFLAGS += -DANDROID -DNDK_BUILD -DCMM_DEBUG -g -ggdb  $(OPTI) -I$(LOCAL_PATH)/..
LOCAL_SRC_FILES := $(addprefix ../, libcmm_test_sender.cpp timeops.cpp)
LOCAL_CFLAGS += -DNOMULTISOCK
LOCAL_STATIC_LIBRARIES += libancillary
LOCAL_SHARED_LIBRARIES := libcmm liblog libinstruments libpowertutor libmocktime
LOCAL_LDLIBS := -llog
include $(BUILD_EXECUTABLE)

# vanilla_test_receiver: vanilla_test_receiver.o timeops.o
#   $(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^

include $(CLEAR_VARS)
LOCAL_MODULE_PATH := $(TARGET_OUT_EXECUTABLES)/libcmm_bin
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE := vanilla_test_receiver
LOCAL_CFLAGS += $(common_CFLAGS)
LOCAL_CFLAGS += -DANDROID -DNDK_BUILD -DCMM_DEBUG -g -ggdb  $(OPTI) -I$(LOCAL_PATH)/..
LOCAL_SRC_FILES := $(addprefix ../, libcmm_test_receiver.cpp timeops.cpp)
LOCAL_SHARED_LIBRARIES := libcmm liblog libinstruments libpowertutor libmocktime
LOCAL_LDLIBS := -llog
LOCAL_CFLAGS += -DNOMULTISOCK
include $(BUILD_EXECUTABLE)

# cmm_throughput_test: libcmm_throughput_test.o libcmm.so
#   $(CXX) $(CXXFLAGS) $(LDFLAGS) $(LIBS) -lcmm -o $@ $<

include $(CLEAR_VARS)
LOCAL_MODULE_PATH := $(TARGET_OUT_EXECUTABLES)/libcmm_bin
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE := cmm_throughput_test
LOCAL_CFLAGS += $(common_CFLAGS)
LOCAL_CFLAGS += -DANDROID -DNDK_BUILD -DCMM_DEBUG -g -ggdb  $(OPTI) -I$(LOCAL_PATH)/..
LOCAL_SRC_FILES := $(addprefix ../, libcmm_throughput_test.cpp)
LOCAL_SHARED_LIBRARIES := libcmm liblog libinstruments libpowertutor libmocktime
LOCAL_LDLIBS := -llog
include $(BUILD_EXECUTABLE)

# vanilla_throughput_test: vanilla_throughput_test.o timeops.o
#   $(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^

include $(CLEAR_VARS)
LOCAL_MODULE_PATH := $(TARGET_OUT_EXECUTABLES)/libcmm_bin
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE := vanilla_throughput_test
LOCAL_CFLAGS += $(common_CFLAGS)
LOCAL_CFLAGS += -DANDROID -DNDK_BUILD -DCMM_DEBUG -g -ggdb  $(OPTI) -I$(LOCAL_PATH)/..
LOCAL_SRC_FILES := $(addprefix ../, libcmm_throughput_test.cpp timeops.cpp)
LOCAL_CFLAGS += -DNOMULTISOCK
LOCAL_SHARED_LIBRARIES := libcmm liblog libinstruments libpowertutor libmocktime
LOCAL_LDLIBS := -llog
include $(BUILD_EXECUTABLE)

# conn_scout: libcmm_scout.o cdf_sampler.o debug.o cmm_thread.o timeops.o

include $(CLEAR_VARS)

# LOCAL_MODULE_TAGS := optional
# LOCAL_MODULE := conn_scout
# LOCAL_SRC_FILES := $(addprefix ../, \
# 	scout/libcmm_scout.cpp cmm_thread.cpp timeops.cpp cdf_sampler.cpp \
#     libcmm_net_preference.cpp)
# LOCAL_CFLAGS += $(common_CFLAGS)
# LOCAL_CFLAGS += -DBUILDING_EXTERNAL -DANDROID -DNDK_BUILD -I$(LOCAL_PATH)/..
# LOCAL_C_INCLUDES += $(LOCAL_PATH)/../
# LOCAL_SHARED_LIBRARIES := liblog
# LOCAL_LDLIBS := -llog

# include $(BUILD_EXECUTABLE)

$(call import-module, edu.umich.mobility/configumerator)
$(call import-module, edu.umich.mobility/instruments)
$(call import-module, edu.umich.mobility/libpowertutor)
$(call import-module, edu.umich.mobility/mocktime)
