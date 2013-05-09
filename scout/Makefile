ifeq ($(wildcard ../Makeconf.local),../Makeconf.local)
include ../Makeconf.local
else
DEBUG_FLAGS:=-g -DCMM_DEBUG
endif
CXXFLAGS+=-Wall -Werror -I.. -I../libancillary -I/usr/local/include -pthread -fPIC -m32 \
	  $(shell pkg-config --cflags --libs glib-2.0) \
	  $(DEBUG_FLAGS) -DBUILDING_SCOUT
LDFLAGS+=-lrt -lglib-2.0 -lboost_thread -lboost_system

all: conn_scout virgil_server

libcmm_shmem_scout.o: ../libcmm_shmem.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $^

conn_scout: libcmm_scout.o ../cdf_sampler.o ../debug.o ../config.o ../common.o \
	    ../cmm_thread.o ../timeops.o libcmm_shmem_scout.o ../libcmm_net_restriction.o \
	    ../libancillary/libancillary.a
	$(CXX) -o $@ $^ $(CXXFLAGS) $(LDFLAGS)

virgil_server: virgil_server.c
	gcc -o $@ $^ -g -Wall -Werror -pthread

.bininstall: conn_scout
	install conn_scout /usr/local/bin/
	-touch .bininstall

install: .bininstall

include ../deps.mk

clean:
	rm -f *~ *.o conn_scout .bininstall 
