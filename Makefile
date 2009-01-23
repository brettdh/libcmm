CXXFLAGS:=-Wall -Werror -g -I.
LIBTBB:=-ltbb_debug
LDFLAGS:=-L.  $(LIBTBB) -lrt

LIBRARIES:=libcmm.so
EXECUTABLES:=conn_scout cmm_test

all: $(LIBRARIES) $(EXECUTABLES)

cmm_test: libcmm_test.o libcmm.so
	$(CXX) $(CFLAGS) $(LDFLAGS) -o $@ $^

conn_scout: libcmm_scout.o
	$(CXX) $(CFLAGS) $(LDFLAGS) -o $@ $^

libcmm.so: libcmm.o libcmm_ipc.o
	$(CXX) $(CFLAGS) $(LDFLAGS) -shared -o $@ $^

libcmm_test.o: libcmm.h
libcmm_scout.o: libcmm.h libcmm_ipc.h
libcmm.o: libcmm.h libcmm_ipc.h timeops.h
libcmm_ipc.o: libcmm_ipc.h

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

.hdrinstall: libcmm.h
	install libcmm.h /usr/local/include/
	-touch .hdrinstall

.bininstall: conn_scout
	install conn_scout /usr/local/bin/
	-touch .bininstall

install: .tbbinstall .libinstall .hdrinstall .bininstall
