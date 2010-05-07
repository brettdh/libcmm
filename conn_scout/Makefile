DEBUG_FLAGS:=-g -DCMM_DEBUG
CXXFLAGS+=-Wall -Werror -I.. -pthread -fPIC -m32 $(DEBUG_FLAGS)

EXECUTABLES := conn_scout

include ../deps.mk

all: $(EXECUTABLES)

%.o: ../%.cpp
	g++ $(CXXFLAGS) -c -o $@ $<

conn_scout: libcmm_scout.o cdf_sampler.o debug.o cmm_thread.o timeops.o
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^

.bininstall: conn_scout
	install conn_scout /usr/local/bin/
	-touch .bininstall

clean:
	rm -f *~ *.o $(EXECUTABLES) .bininstall 
