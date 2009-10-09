CXXFLAGS:=-Wall -Werror -I. -I.. -m32 -g -DCMM_UNIT_TESTING #-DCMM_DEBUG 
LDFLAGS:=-L. -L.. -lcppunit -ldl -ltbb

EXECUTABLES=run_unit_tests run_lib_tests run_remote_tests run_thunk_tests run_trickle_tests

.gitignore: Makefile
	echo "$(EXECUTABLES)" | sed -e 's/\s+/ /' | tr ' ' '\n' > .gitignore

all: $(EXECUTABLES)

test: run_unit_tests run_lib_tests
	./run_unit_tests; ./run_lib_tests

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

timeops.o: ../timeops.cpp ../timeops.h
	g++ $(CXXFLAGS) -c -o $@ $<

TESTOBJS := pending_irob_test.o lattice_test.o intset_test.o \
	    receiver_lattice_test.o pending_sender_irob_test.o
SUPPORTOBJS := pending_irob.o intset.o debug.o pending_receiver_irob.o \
	       pending_sender_irob.o timeops.o

run_unit_tests: run_all_tests.o $(TESTOBJS) $(SUPPORTOBJS)
	g++ -o $@ $^ $(CXXFLAGS) $(LDFLAGS)

LIBTESTOBJS:=end_to_end_tests_base.o end_to_end_tests_forked.o

run_lib_tests: run_all_tests.o $(LIBTESTOBJS)
	g++ -o $@ $^ $(CXXFLAGS) $(LDFLAGS) -lcmm -lssl

REMOTETESTOBJS:=end_to_end_tests_base.o end_to_end_tests_remote.o remote_tests.o
run_remote_tests: run_all_tests.o $(REMOTETESTOBJS)
	g++ -o $@ $^ $(CXXFLAGS) $(LDFLAGS) -lcmm -lssl

THUNKTESTOBJS:=end_to_end_tests_base.o end_to_end_tests_remote.o thunk_tests.o
run_thunk_tests: run_all_tests.o $(THUNKTESTOBJS)
	g++ -o $@ $^ $(CXXFLAGS) $(LDFLAGS) -lcmm -lssl -pthread

TRICKLETESTOBJS:=end_to_end_tests_base.o end_to_end_tests_remote.o trickle_tests.o
run_trickle_tests: run_all_tests.o $(TRICKLETESTOBJS)
	g++ -o $@ $^ $(CXXFLAGS) $(LDFLAGS) -lcmm -lssl -pthread

clean:
	rm -f *.o *~ $(EXECUTABLES)
