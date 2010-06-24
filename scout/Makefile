DEBUG_FLAGS:=-g -DCMM_DEBUG
CXXFLAGS+=-Wall -Werror -I.. -I/usr/local/include -pthread -fPIC -m32 \
	  $(DEBUG_FLAGS)

all: conn_scout

conn_scout: libcmm_scout.o ../cdf_sampler.o ../debug.o \
	    ../cmm_thread.o ../timeops.o
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^

.bininstall: conn_scout
	install conn_scout /usr/local/bin/
	-touch .bininstall

install: .bininstall

include ../deps.mk

clean:
	rm -f *~ *.o conn_scout .bininstall 
