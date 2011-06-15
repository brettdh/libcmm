CXXFLAGS:=-Wall -Werror -I. -I.. -I../libancillary -m32 -g -DCMM_UNIT_TESTING -DCMM_DEBUG -pthread \
	  -I/usr/include/glib-2.0 -I/usr/lib/glib-2.0/include
LDFLAGS:=-L. -L..  -lcppunit -ldl -pthread -lrt -lglib-2.0 -lboost_thread

EXECUTABLES=run_unit_tests run_lib_tests run_remote_tests run_thunk_tests run_trickle_tests \
	    run_lib_tests_nb run_remote_tests_nb multi_app_test multi_app_test_helper \
		fake_intnw_test run_spotty_network_failure_test

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


shmem_test_helper: shmem_test_helper.o shmem_test_common.o libcmm_shmem.o common.o \
		   timeops.o debug.o test_common.o ../libancillary/libancillary.a csocket.o

TESTOBJS := pending_irob_test.o lattice_test.o intset_test.o \
	    receiver_lattice_test.o pending_sender_irob_test.o \
	    ack_timeouts_test.o pending_receiver_irob_test.o \
	    estimation_test.o pthread_util_test.o 
SUPPORTOBJS := pending_irob.o intset.o debug.o pending_receiver_irob.o \
	       pending_sender_irob.o timeops.o ack_timeouts.o net_stats.o \
	       test_common.o debug.o net_interface.o irob_scheduling.o common.o \
	       shmem_test.o shmem_test_common.o libcmm_shmem.o ../libancillary/libancillary.a csocket.o

run_unit_tests:: shmem_test_helper
run_unit_tests:: run_all_tests.o StdioOutputter.o $(TESTOBJS) $(SUPPORTOBJS)
	g++ -o $@ $^ $(CXXFLAGS) $(LDFLAGS)

LIBTESTOBJS:=end_to_end_tests_base.o end_to_end_tests_forked.o forked_tests.o

run_lib_tests: run_all_tests.o StdioOutputter.o test_common.o $(LIBTESTOBJS)
	g++ -o $@ $^ $(CXXFLAGS) $(LDFLAGS) -lcmm -lssl

REMOTETESTOBJS:=end_to_end_tests_base.o end_to_end_tests_remote.o remote_tests.o socket_api_tests.o
run_remote_tests: run_all_tests.o StdioOutputter.o test_common.o $(REMOTETESTOBJS)
	g++ -o $@ $^ $(CXXFLAGS) $(LDFLAGS) -lcmm -lssl


NBOBJS:=end_to_end_tests_base.o non_blocking_tests.o 

LIBTESTNBOBJS:=end_to_end_tests_forked.o non_blocking_tests_forked.o
REMOTETESTNBOBJS:=end_to_end_tests_remote.o non_blocking_tests_remote.o

run_lib_tests_nb: run_all_tests.o StdioOutputter.o test_common.o $(NBOBJS) $(LIBTESTNBOBJS)
	g++ -o $@ $^ $(CXXFLAGS) $(LDFLAGS) -lcmm -lssl
run_remote_tests_nb: run_all_tests.o StdioOutputter.o test_common.o $(NBOBJS) $(REMOTETESTNBOBJS)
	g++ -o $@ $^ $(CXXFLAGS) $(LDFLAGS) -lcmm -lssl



THUNKTESTOBJS:=end_to_end_tests_base.o end_to_end_tests_remote.o thunk_tests.o
run_thunk_tests: run_all_tests.o StdioOutputter.o test_common.o $(THUNKTESTOBJS)
	g++ -o $@ $^ $(CXXFLAGS) $(LDFLAGS) -lcmm -lssl -pthread

TRICKLETESTOBJS:=end_to_end_tests_base.o end_to_end_tests_remote.o trickle_tests.o
run_trickle_tests: run_all_tests.o StdioOutputter.o $(TRICKLETESTOBJS) test_common.o
	g++ -o $@ $^ $(CXXFLAGS) $(LDFLAGS) -lcmm -lssl -pthread

multi_app_test_helper_main.o: multi_app_test_helper.cpp
	g++ -c -o $@ $^ $(CXXFLAGS) -DMULTI_APP_TEST_EXECUTABLE

multi_app_test_helper: multi_app_test_helper_main.o test_common.o
	g++ -o $@ $^ $(CXXFLAGS) $(LDFLAGS) -lcmm -pthread

multi_app_test: multi_app_test.o multi_app_test_helper.o test_common.o
	g++ -o $@ $^ $(CXXFLAGS) $(LDFLAGS) -lcmm -pthread


fake_intnw_test: run_all_tests.o spotty_network_failure_test.o \
			     end_to_end_tests_base.o end_to_end_tests_remote.o \
			     net_interface.o test_common.o cmm_socket_control.o StdioOutputter.o
	g++ -o $@ $^ $(CXXFLAGS) $(LDFLAGS) -lcmm -lssl

run_spotty_network_failure_test:: run_spotty_network_failure_test.o
	g++ -o $@ $^ $(CXXFLAGS) $(LDFLAGS)

include ../deps.mk

clean:
	rm -f *.o *~ $(EXECUTABLES)
