CXXFLAGS:=-Wall -Werror -I. -I.. -m32 -g #-DCMM_DEBUG 
LDFLAGS:=-L. -L.. -lcppunit -ldl -ltbb

test: run_tests
	./run_tests

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

TESTOBJS := pending_irob_test.o lattice_test.o intset_test.o
SUPPORTOBJS := pending_irob.o intset.o debug.o

run_tests: run_tests.o $(TESTOBJS) $(SUPPORTOBJS)
	g++ -o $@ $^ $(CXXFLAGS) $(LDFLAGS)


clean:
	rm -f *.o *~ run_tests
