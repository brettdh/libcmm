CXXFLAGS:=-Wall -Werror -I. -I.. -m32 -g #-DCMM_DEBUG 
LDFLAGS:=-L. -lcppunit -ldl

test: run_tests
	./run_tests

pending_irob.o: ../pending_irob.cpp ../pending_irob.h
	g++ $(CXXFLAGS) -c -o $@ $<

pending_receiver_irob.o: ../pending_receiver_irob.cpp ../pending_receiver_irob.h
	g++ $(CXXFLAGS) -c -o $@ $<

run_tests: run_tests.o pending_irob_test.o pending_irob.o lattice_test.o \
	   receiver_lattice_test.o ../intset.o ../debug.o pending_receiver_irob.o
	g++ -o $@ $^ $(CXXFLAGS) $(LDFLAGS)


clean:
	rm -f *.o *~ run_tests
