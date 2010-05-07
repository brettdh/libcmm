# Generate header dependency rules
#   see http://stackoverflow.com/questions/204823/
# ---
SRCS=$(wildcard *.cpp)
DEPS=$(SRCS:%.cpp=.%.dep)

.%.dep: %.cpp
	g++ -MM $(CXXFLAGS) $< >$@

include $(DEPS)

#depend: $(SRCS)
#	g++ -MM $(CXXFLAGS) $(SRCS) >depend

#include depend
# ---
