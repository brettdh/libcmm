ifeq ($(wildcard ../Makeconf.local),../Makeconf.local)
include ../Makeconf.local
else
DEBUG_FLAGS:=-g -DCMM_DEBUG
endif
CXXFLAGS+=-Wall -Werror -I.. -I/usr/local/include -pthread -fPIC -m32 \
	   -I/usr/include/glib-2.0 -I/usr/lib/glib-2.0/include \
	  $(DEBUG_FLAGS)
LDFLAGS+=-lrt -lglib-2.0 -lboost_thread

all: conn_scout

conn_scout: libcmm_scout.o ../cdf_sampler.o ../debug.o ../common.o \
	    ../cmm_thread.o ../timeops.o ../libcmm_shmem.o \
	    ../libancillary/libancillary.a
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^

.bininstall: conn_scout
	install conn_scout /usr/local/bin/
	-touch .bininstall

install: .bininstall

include ../deps.mk

clean:
	rm -f *~ *.o conn_scout .bininstall 
