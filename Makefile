ifeq ($(wildcard Makeconf.local),Makeconf.local)
include Makeconf.local
else
DEBUG_FLAGS:=-g -DCMM_DEBUG
endif

INSTRUMENTS_DIR := $(HOME)/src/instruments

NODEBUG_CXXFLAGS := $(CXXFLAGS) 
NODEBUG_CXXFLAGS +=-Wall -Werror -I. -I/usr/local/include \
	   -I./libancillary \
	   -pthread -fPIC -m32 -std=c++11  $(OPT_FLAGS) \
	   -I$(INSTRUMENTS_DIR)/include -I$(INSTRUMENTS_DIR)/src
CXXFLAGS := $(NODEBUG_CXXFLAGS) $(DEBUG_FLAGS)
#LIBTBB:=-ltbb_debug
LDFLAGS:=-L.  -L/usr/local/lib -L$(INSTRUMENTS_DIR)/src -m32
LIBS:=-lrt -lboost_thread -lpowertutor -linstruments -lconfigumerator

LIBRARIES:=libcmm.so libflipflop.a
EXECUTABLES:=cmm_test_sender cmm_test_receiver cdf_test\
	     vanilla_test_sender vanilla_test_receiver \
	     cmm_throughput_test vanilla_throughput_test simple_http_client

all: $(LIBRARIES) $(EXECUTABLES) subdirs

SUBDIRS := scout
.PHONY: subdirs $(SUBDIRS)
subdirs: $(SUBDIRS)
$(SUBDIRS)::
	make -C $@

cdf_test: cdf_test.o cdf_sampler.o timeops.o
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^

cmm_test_sender: libcmm_test_sender.o libcmm.so 
	$(CXX) -o $@ $^ $(CXXFLAGS) $(LDFLAGS) $(LIBS)

simple_http_client: simple_http_client.o libcmm.so 
	$(CXX) -o $@ $^ $(CXXFLAGS) $(LDFLAGS) $(LIBS)

cmm_test_receiver: libcmm_test_receiver.o libcmm.so 
	$(CXX) -o $@ $^ $(CXXFLAGS) $(LDFLAGS) $(LIBS) 

vanilla_test_sender: vanilla_test_sender.o timeops.o 
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^

vanilla_test_receiver: vanilla_test_receiver.o timeops.o 
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^

cmm_throughput_test: libcmm_throughput_test.o libcmm.so 
	$(CXX) -o $@ $^ $(CXXFLAGS) $(LDFLAGS) $(LIBS) 

vanilla_throughput_test: vanilla_throughput_test.o timeops.o
	$(CXX) -o $@ $^ $(CXXFLAGS) $(LDFLAGS)

vanilla_%.o: libcmm_%.cpp
	$(CXX) $(CXXFLAGS) -DNOMULTISOCK $(LDFLAGS) -c -o $@ $<

debug_ext.o: debug_ext.cpp
	$(CXX) $(NODEBUG_CXXFLAGS) -c -o $@ $<

flipflop_estimate_ext.o: flipflop_estimate.cpp
	$(CXX) $(NODEBUG_CXXFLAGS) -c -o $@ $<

libflipflop.a: flipflop_estimate_ext.o debug_ext.o
	rm -f $@
	ar rcs $@ $^

libcmm.so: libcmm.o libcmm_ipc.o libcmm_external_ipc.o libcmm_net_restriction.o \
	       cmm_socket.o cmm_socket_impl.o \
	       cmm_socket_passthrough.o thunks.o cmm_timing.o csocket.o \
           csocket_mapping.o csocket_sender.o csocket_receiver.o \
		   flipflop_estimate.o \
           pending_irob.o pending_sender_irob.o pending_receiver_irob.o \
           cmm_thread.o cmm_internal_listener.o libcmm_irob.o debug.o intnw_config.o \
           intset.o cmm_socket_control.o irob_scheduling.o timeops.o \
           net_interface.o net_stats.o cmm_conn_bootstrapper.o \
           redundancy_strategy.o \
		   network_chooser.o network_chooser_impl.o \
		   intnw_instruments_network_chooser.o \
		   intnw_instruments_net_stats_wrapper.o \
           libcmm_shmem.o common.o libancillary/libancillary.a
	$(CXX) -shared -o $@ $^ $(CXXFLAGS) $(LDFLAGS) $(LIBS)

.PHONY: libcmm.tgz
libcmm.tgz:
	tar czf ./libcmm.tgz *.h *.cpp Makefile Makeconf.local.sample README.txt tests/*.cpp tests/*.h tests/Makefile

include deps.mk

.PHONY: subdirclean
subdirclean:
ifdef SUBDIRS
	@for d in $(SUBDIRS) ; do \
	    ${MAKE} -C $$d clean; \
	done
endif

clean: subdirclean
	rm -f *~ *.o $(LIBRARIES) $(EXECUTABLES) .libinstall .hdrinstall libcmm.tgz

#TBB_LIBS:=libtbbmalloc_debug.so libtbbmalloc.so.2 libtbb_debug.so \
#          libtbbmalloc_debug.so.2 libtbb.so libtbb_debug.so.2 \
#          libtbbmalloc.so libtbb.so.2

#.tbbinstall: $(TBB_LIBS)
#	install $(TBB_LIBS) /usr/local/lib
#	-touch .tbbinstall

.libinstall: $(LIBRARIES)
	install libcmm.so /usr/local/lib/
	install libflipflop.a /usr/local/lib/
	-touch .libinstall

.hdrinstall: libcmm.h libcmm_irob.h
	install libcmm.h /usr/local/include/
	install libcmm_irob.h /usr/local/include/
	install libcmm_net_restriction.h /usr/local/include/
	-touch .hdrinstall

.PHONY: scout_install
scout_install:
	make -C scout install

install: .libinstall .hdrinstall scout_install

.gitignore: Makefile
	echo "$(EXECUTABLES) $(LIBRARIES) .*.dep *~ *.o .*install libcmm.tgz" | sed -e 's/\s+/ /' | tr ' ' '\n' > .gitignore
