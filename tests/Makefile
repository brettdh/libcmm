CXXFLAGS:=-Wall -Werror -I. -I.. -m32 -g -DCMM_DEBUG 
LDFLAGS:=-L. -lcppunit -ldl

test: run_tests
	./run_tests

run_tests: run_tests.o pending_irob_test.o ../pending_irob.o \
	   ../intset.o ../debug.o
	g++ -o $@ $^ $(CXXFLAGS) $(LDFLAGS)


clean:
	rm -f *.o *~ run_tests
