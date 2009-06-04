CXXFLAGS := -Wall -Werror -I.
DEBUGFLAG := -g -DDETAILED_LOGGING
OPTFLAG := -O2

all: rpc3proxy rpc3proxy_opt

rpc3proxy: proxy.o rpc3proxy.o
	g++ -o $@ $^

rpc3proxy_opt: proxy_opt.o rpc3proxy_opt.o
	g++ $(OPTFLAG) -o $@ $^

%_opt.o: %.cpp
	g++ $(CXXFLAGS) $(OPTFLAG) -c -o $@ $<

%.o: %.cpp
	g++ $(CXXFLAGS) $(DEBUGFLAG) -c -o $@ $<

clean:
	rm -f *.o *~ rpc3proxy rpc3proxy_opt
