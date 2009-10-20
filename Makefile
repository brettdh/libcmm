ifeq ($(wildcard Makeconf.local),Makeconf.local)
include Makeconf.local
else
DEBUG_FLAGS:=-g -DCMM_DEBUG
endif

CXXFLAGS:=-Wall -Werror -I. -pthread -fpic -m32 $(DEBUG_FLAGS) $(OPT_FLAGS)
LIBTBB:=-ltbb_debug
LDFLAGS:=-L. -m32 
LIBS:=$(LIBTBB) -lrt

LIBRARIES:=libcmm.so
EXECUTABLES:=conn_scout cmm_test_sender cmm_test_receiver cdf_test\
	     vanilla_test_sender vanilla_test_receiver \
	     cmm_throughput_test vanilla_throughput_test

.gitignore: Makefile
	echo "$(EXECUTABLES) $(LIBRARIES)" | sed -e 's/\s+/ /' | tr ' ' '\n' > .gitignore

all: $(LIBRARIES) $(EXECUTABLES)

cdf_test: cdf_test.o cdf_sampler.o
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^

cmm_test_sender: libcmm_test_sender.o libcmm.so 
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(LIBS) -lcmm -o $@ $<

cmm_test_receiver: libcmm_test_receiver.o libcmm.so 
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(LIBS) -lcmm -o $@ $<

vanilla_test_sender: vanilla_test_sender.o timeops.o
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^ -lrt

vanilla_test_receiver: vanilla_test_receiver.o timeops.o
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^ -lrt

cmm_throughput_test: libcmm_throughput_test.o libcmm.so
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(LIBS) -lcmm -o $@ $<

vanilla_throughput_test: vanilla_throughput_test.o timeops.o
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^ -lrt

vanilla_%.o: libcmm_%.cpp
	$(CXX) $(CXXFLAGS) -DNOMULTISOCK $(LDFLAGS) -c -o $@ $<

conn_scout: libcmm_scout.o cdf_sampler.o debug.o cmm_thread.o timeops.o
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(LIBS) -o $@ $^

libcmm.so: libcmm.o libcmm_ipc.o cmm_socket.o cmm_socket_impl.o \
	   cmm_socket_passthrough.o thunks.o cmm_timing.o signals.o csocket.o \
           csocket_mapping.o csocket_sender.o csocket_receiver.o \
           pending_irob.o pending_sender_irob.o pending_receiver_irob.o \
           cmm_thread.o cmm_internal_listener.o libcmm_irob.o debug.o \
           intset.o cmm_socket_control.o irob_scheduling.o timeops.o \
	   ack_timeouts.o net_interface.o
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(LIBS) -shared -o $@ $^

# Generate header dependency rules
#   see http://stackoverflow.com/questions/204823/
# ---
SRCS=$(wildcard *.cpp)
DEPS=$(SRCS:%.cpp=.%.dep)

.%.dep: %.cpp
	g++ -MM $(CXXFLAGS) $< >$@

include $(DEPS)

#depend: $(SRCS)
#	g++ -MM $(CXXFLAGS) $(SRCS) >depend

#include depend
# ---

clean:
	rm -f *~ *.o $(LIBRARIES) $(EXECUTABLES) .tbbinstall .libinstall .hdrinstall .bininstall

TBB_LIBS:=libtbbmalloc_debug.so libtbbmalloc.so.2 libtbb_debug.so \
          libtbbmalloc_debug.so.2 libtbb.so libtbb_debug.so.2 \
          libtbbmalloc.so libtbb.so.2

.tbbinstall: $(TBB_LIBS)
	install $(TBB_LIBS) /usr/local/lib
	-touch .tbbinstall

.libinstall: libcmm.so 
	install libcmm.so /usr/local/lib/
	-touch .libinstall

.hdrinstall: libcmm.h libcmm_irob.h
	install libcmm.h /usr/local/include/
	install libcmm_irob.h /usr/local/include/
	-touch .hdrinstall

.bininstall: conn_scout
	install conn_scout /usr/local/bin/
	-touch .bininstall

install: .tbbinstall .libinstall .hdrinstall .bininstall
