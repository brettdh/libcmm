CXXFLAGS:=-Wall -Werror -I. -I.. -m32 -g -DCMM_UNIT_TESTING #-DCMM_DEBUG 
LDFLAGS:=-L. -L.. -lcppunit -ldl -ltbb

all: run_unit_tests run_lib_tests

test: run_unit_tests
	./run_unit_tests

%.o: %.cpp %.h
	g++ $(CXXFLAGS) -c -o $@ $<

%.o: ../%.cpp ../%.h
	g++ $(CXXFLAGS) -c -o $@ $<

%.o: %.cpp
	g++ $(CXXFLAGS) -c -o $@ $<

%.o: ../%.cpp
	g++ $(CXXFLAGS) -c -o $@ $<

pending_irob.o: ../pending_irob.cpp ../pending_irob.h
	g++ $(CXXFLAGS) -c -o $@ $<

pending_receiver_irob.o: ../pending_receiver_irob.cpp ../pending_receiver_irob.h
	g++ $(CXXFLAGS) -c -o $@ $<

TESTOBJS := pending_irob_test.o lattice_test.o intset_test.o receiver_lattice_test.o
SUPPORTOBJS := pending_irob.o intset.o debug.o pending_receiver_irob.o

run_unit_tests: run_all_tests.o $(TESTOBJS) $(SUPPORTOBJS)
	g++ -o $@ $^ $(CXXFLAGS) $(LDFLAGS)

LIBTESTOBJS:=end_to_end_tests_forked.o

run_lib_tests: run_all_tests.o $(LIBTESTOBJS)
	g++ -o $@ $^ $(CXXFLAGS) $(LDFLAGS) -lcmm -lssl

clean:
	rm -f *.o *~ run_unit_tests run_lib_tests
