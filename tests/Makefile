INSTRUMENTS_DIR := $(HOME)/src/instruments

CXXFLAGS:=-Wall -Werror -I. -I.. -I../libancillary -m32 -g -DCMM_DEBUG -pthread -std=gnu++0x \
	   $(shell pkg-config --cflags --libs glib-2.0) \
	   -I$(INSTRUMENTS_DIR)/src -I$(INSTRUMENTS_DIR)/include
LDFLAGS:=-L. -L..  -lcppunit -ldl -pthread -lrt -lglib-2.0 -lboost_thread

EXECUTABLES=run_unit_tests run_lib_tests run_remote_tests run_thunk_tests run_trickle_tests \
	    run_lib_tests_nb run_remote_tests_nb multi_app_test multi_app_test_helper \
		fake_intnw_test run_spotty_network_failure_test run_socket_api_tests

.gitignore: Makefile
	echo "$(EXECUTABLES)" | sed -e 's/\s+/ /' | tr ' ' '\n' > .gitignore

all: $(EXECUTABLES)

test: run_unit_tests run_lib_tests
	./run_unit_tests; ./run_lib_tests

%.o: %.cpp %.h
	g++ $(CXXFLAGS) -DCMM_UNIT_TESTING -c -o $@ $<

%.o: ../%.cpp ../%.h
	g++ $(CXXFLAGS) -DCMM_UNIT_TESTING -c -o $@ $<

%.o: %.cpp
	g++ $(CXXFLAGS) -DCMM_UNIT_TESTING -c -o $@ $<

%.o: ../%.cpp
	g++ $(CXXFLAGS) -DCMM_UNIT_TESTING -c -o $@ $<

pending_irob.o: ../pending_irob.cpp ../pending_irob.h
	g++ $(CXXFLAGS) -DCMM_UNIT_TESTING -c -o $@ $<

pending_receiver_irob.o: ../pending_receiver_irob.cpp ../pending_receiver_irob.h
	g++ $(CXXFLAGS) -DCMM_UNIT_TESTING -c -o $@ $<

timeops.o: ../timeops.cpp ../timeops.h
	g++ $(CXXFLAGS) -DCMM_UNIT_TESTING -c -o $@ $<

run_all_tests_unit.o: run_all_tests.cpp
	g++ $(CXXFLAGS) -DCMM_UNIT_TESTING -c -o $@ $<

run_all_tests.o: run_all_tests.cpp
	g++ $(CXXFLAGS) -c -o $@ $<


shmem_test_helper: shmem_test_helper.o shmem_test_common.o libcmm_shmem.o common.o \
		   timeops.o debug.o config.o test_common.o ../libancillary/libancillary.a csocket.o
	g++ -o $@ $^ $(CXXFLAGS) $(LDFLAGS) -pthread

TESTOBJS := pending_irob_test.o lattice_test.o intset_test.o \
	    receiver_lattice_test.o pending_sender_irob_test.o \
	    pending_receiver_irob_test.o \
	    pthread_util_test.o net_interface_test.o util_test.o \
	    estimation_test.o 
SUPPORTOBJS := pending_irob.o intset.o debug.o config.o pending_receiver_irob.o \
	       pending_sender_irob.o timeops.o net_stats.o \
	       test_common.o debug.o config.o net_interface.o irob_scheduling.o common.o \
	       csocket.o redundancy_strategy.o  \
		   cmm_socket_control.o libcmm_net_restriction.o
# not running shmem_test for now; it's broken somehow
#shmem_test.o shmem_test_common.o libcmm_shmem.o ../libancillary/libancillary.a 

run_unit_tests:: shmem_test_helper
run_unit_tests:: run_all_tests_unit.o StdioOutputter.o $(TESTOBJS) $(SUPPORTOBJS)
	g++ -o $@ $^ $(CXXFLAGS) -DCMM_UNIT_TESTING $(LDFLAGS)

LIBTESTOBJS:=end_to_end_tests_base.o end_to_end_tests_forked.o forked_tests.o

run_lib_tests: run_all_tests.o StdioOutputter.o test_common.o $(LIBTESTOBJS)
	g++ -o $@ $^ $(CXXFLAGS) $(LDFLAGS) -lcmm -lssl -lcrypto

SOCKETAPITESTOBJS:=end_to_end_tests_base.o end_to_end_tests_forked.o socket_api_tests.o

run_socket_api_tests: run_all_tests.o StdioOutputter.o test_common.o $(SOCKETAPITESTOBJS)
	g++ -o $@ $^ $(CXXFLAGS) $(LDFLAGS) -lcmm -lssl -lcrypto



REMOTETESTOBJS:=end_to_end_tests_base.o end_to_end_tests_remote.o remote_tests.o
run_remote_tests: run_all_tests.o StdioOutputter.o test_common.o $(REMOTETESTOBJS)
	g++ -o $@ $^ $(CXXFLAGS) $(LDFLAGS) -lcmm -lssl -lcrypto


NBOBJS:=end_to_end_tests_base.o non_blocking_tests.o 

LIBTESTNBOBJS:=end_to_end_tests_forked.o non_blocking_tests_forked.o
REMOTETESTNBOBJS:=end_to_end_tests_remote.o non_blocking_tests_remote.o

run_lib_tests_nb: run_all_tests.o StdioOutputter.o test_common.o $(NBOBJS) $(LIBTESTNBOBJS)
	g++ -o $@ $^ $(CXXFLAGS) $(LDFLAGS) -lcmm -lssl -lcrypto
run_remote_tests_nb: run_all_tests.o StdioOutputter.o test_common.o $(NBOBJS) $(REMOTETESTNBOBJS)
	g++ -o $@ $^ $(CXXFLAGS) $(LDFLAGS) -lcmm -lssl -lcrypto



THUNKTESTOBJS:=end_to_end_tests_base.o end_to_end_tests_remote.o thunk_tests.o
run_thunk_tests: run_all_tests.o StdioOutputter.o test_common.o $(THUNKTESTOBJS)
	g++ -o $@ $^ $(CXXFLAGS) $(LDFLAGS) -lcmm -lssl -lcrypto -pthread

TRICKLETESTOBJS:=end_to_end_tests_base.o end_to_end_tests_remote.o trickle_tests.o
run_trickle_tests: run_all_tests.o StdioOutputter.o $(TRICKLETESTOBJS) test_common.o
	g++ -o $@ $^ $(CXXFLAGS) $(LDFLAGS) -lcmm -lssl -lcrypto -pthread

multi_app_test_helper_main.o: multi_app_test_helper.cpp
	g++ -c -o $@ $^ $(CXXFLAGS) -DMULTI_APP_TEST_EXECUTABLE

multi_app_test_helper: multi_app_test_helper_main.o test_common.o
	g++ -o $@ $^ $(CXXFLAGS) $(LDFLAGS) -lcmm -pthread

multi_app_test: multi_app_test.o multi_app_test_helper.o test_common.o
	g++ -o $@ $^ $(CXXFLAGS) $(LDFLAGS) -lcmm -pthread


fake_intnw_test: run_all_tests.o spotty_network_failure_test.o proxy_socket.o \
			     end_to_end_tests_base.o end_to_end_tests_remote.o \
			     net_interface.o test_common.o cmm_socket_control.o StdioOutputter.o timeops.o
	g++ -o $@ $^ $(CXXFLAGS) $(LDFLAGS) -lcmm -lssl -lcrypto

run_spotty_network_failure_test:: run_spotty_network_failure_test.o
	g++ -o $@ $^ $(CXXFLAGS) $(LDFLAGS)

proxy_socket_test: proxy_socket_test.o proxy_socket.o test_common.o debug.o config.o timeops.o
	g++ -o $@ $^ $(CXXFLAGS) $(LDFLAGS)

include ../deps.mk

clean:
	rm -f *.o *~ $(EXECUTABLES)
