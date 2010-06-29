ifeq ($(wildcard Makeconf.local),Makeconf.local)
include Makeconf.local
else
DEBUG_FLAGS:=-g -DCMM_DEBUG
endif

CXXFLAGS+=-Wall -Werror -I. -I/usr/local/include \
	   -I/usr/include/glib-2.0 -I/usr/lib/glib-2.0/include \
	   -pthread -fPIC -m32 $(DEBUG_FLAGS) $(OPT_FLAGS)
#LIBTBB:=-ltbb_debug
LDFLAGS:=-L.  -L/usr/local/lib -m32
LIBS:=-lrt

LIBRARIES:=libcmm.so
EXECUTABLES:=cmm_test_sender cmm_test_receiver cdf_test\
	     vanilla_test_sender vanilla_test_receiver \
	     cmm_throughput_test vanilla_throughput_test

all: $(LIBRARIES) $(EXECUTABLES) subdirs

SUBDIRS := scout
.PHONY: subdirs $(SUBDIRS)
subdirs: $(SUBDIRS)
$(SUBDIRS):
	make -C $@

cdf_test: cdf_test.o cdf_sampler.o debug.o timeops.o
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^

cmm_test_sender: libcmm_test_sender.o libcmm.so debug.o
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(LIBS) -lcmm -o $@ $<

cmm_test_receiver: libcmm_test_receiver.o libcmm.so debug.o
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(LIBS) -lcmm -o $@ $<

vanilla_test_sender: vanilla_test_sender.o timeops.o debug.o
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^

vanilla_test_receiver: vanilla_test_receiver.o timeops.o debug.o
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^

cmm_throughput_test: libcmm_throughput_test.o libcmm.so debug.o
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(LIBS) -lcmm -o $@ $<

vanilla_throughput_test: vanilla_throughput_test.o timeops.o debug.o
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^

vanilla_%.o: libcmm_%.cpp
	$(CXX) $(CXXFLAGS) -DNOMULTISOCK $(LDFLAGS) -c -o $@ $<

libcmm.so: libcmm.o libcmm_ipc.o cmm_socket.o cmm_socket_impl.o \
	   cmm_socket_passthrough.o thunks.o cmm_timing.o csocket.o \
           csocket_mapping.o csocket_sender.o csocket_receiver.o \
           pending_irob.o pending_sender_irob.o pending_receiver_irob.o \
           cmm_thread.o cmm_internal_listener.o libcmm_irob.o debug.o \
           intset.o cmm_socket_control.o irob_scheduling.o timeops.o \
	   ack_timeouts.o net_interface.o net_stats.o cmm_conn_bootstrapper.o \
	   libcmm_shmem.o
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(LIBS) -shared -o $@ $^

.PHONY: libcmm.tgz
libcmm.tgz:
	tar czf ./libcmm.tgz *.h *.cpp Makefile Makeconf.local.sample README.txt tests/*.cpp tests/*.h tests/Makefile

include deps.mk

clean:
	rm -f *~ *.o $(LIBRARIES) $(EXECUTABLES) .libinstall .hdrinstall libcmm.tgz

#TBB_LIBS:=libtbbmalloc_debug.so libtbbmalloc.so.2 libtbb_debug.so \
#          libtbbmalloc_debug.so.2 libtbb.so libtbb_debug.so.2 \
#          libtbbmalloc.so libtbb.so.2

#.tbbinstall: $(TBB_LIBS)
#	install $(TBB_LIBS) /usr/local/lib
#	-touch .tbbinstall

.libinstall: libcmm.so 
	install libcmm.so /usr/local/lib/
	-touch .libinstall

.hdrinstall: libcmm.h libcmm_irob.h
	install libcmm.h /usr/local/include/
	install libcmm_irob.h /usr/local/include/
	-touch .hdrinstall

.PHONY: scout_install
scout_install:
	make -C scout install

install: .libinstall .hdrinstall scout_install

.gitignore: Makefile
	echo "$(EXECUTABLES) $(LIBRARIES) .*.dep *~ *.o .*install libcmm.tgz" | sed -e 's/\s+/ /' | tr ' ' '\n' > .gitignore
